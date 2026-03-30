# Viva Prep

This document contains potential viva questions with detailed answers, covering all assignment requirements.

---

### 1. TCP States: `TIME_WAIT` vs `CLOSE_WAIT`

**Question:** Explain the difference between the `TIME_WAIT` and `CLOSE_WAIT` TCP states. What problems can each cause, and how does our server handle them?

**Answer:**

*   **`CLOSE_WAIT`**: This state occurs on the side that receives the first FIN (passive closer). It means the remote end has closed its side, but the local application hasn't called `closesocket()` yet.
    *   **Problem**: A large number of CLOSE_WAIT sockets = **resource leak**. The application is not properly closing sockets after the peer disconnects. This exhausts file descriptors (or HANDLE quota on Windows).
    *   **Our handling**: In `server.c`, when `websocket_frame_recv()` returns a value ≤ 0, we immediately call `closesocket(sd)` and reset the client slot (`clients[i].sock = INVALID_SOCKET`). This prevents CLOSE_WAIT accumulation.

*   **`TIME_WAIT`**: This state occurs on the side that initiates the active close (sends the first FIN). The socket remains in TIME_WAIT for 2×MSL (Maximum Segment Lifetime, typically 60 seconds on Windows) to ensure any delayed packets from the old connection don't interfere with new connections on the same port.
    *   **Problem**: Rapid server restarts or high connection churn can cause thousands of TIME_WAIT sockets, preventing new binds to the same port.
    *   **Our handling**: `SO_REUSEADDR` is set on the server socket. This tells the OS to allow binding to a port that still has sockets in TIME_WAIT. Without this, restarting the server within 60 seconds would fail with "Address already in use."

*   **How to observe**: Run `netstat -an | findstr :8080` during a load test to see connections in various TCP states.

---

### 2. `select()` vs Threads

**Question:** Our server uses a single-threaded, `select()`-based concurrency model. Compare this to a multi-threaded approach.

**Answer:**

| Aspect | `select()` (our approach) | Multi-threaded |
|--------|--------------------------|----------------|
| **Overhead** | Low — single thread, no context switching | High — each thread needs its own stack (~1MB) |
| **Synchronization** | Not needed — single thread accesses all data | Required — mutexes, semaphores for shared data |
| **Race conditions** | Impossible | Common source of bugs |
| **CPU utilization** | Cannot use multiple cores | Can leverage multi-core CPUs |
| **Blocking operations** | Blocks ALL clients | Only blocks that thread |
| **Scalability** | O(n) per iteration (scan all FDs) | O(1) per thread wake-up |
| **Complexity** | Simpler to implement and debug | More complex, harder to debug |

**Why `select()` for our server:**
- Our server is **I/O-bound**: it mostly waits for data, not computing
- Each message processing is fast (parse JSON, write to DB, broadcast)
- No thread synchronization needed for the client list
- `select()` on Windows supports up to `FD_SETSIZE` (64 by default, can be increased) sockets
- For our scale (30 concurrent clients), `select()` is more than sufficient

**Weakness:** The `db_add_location()` call is synchronous. A slow disk write blocks all clients momentarily. Mitigation: SQLite WAL mode makes writes fast, and we could add a write queue for production.

---

### 3. WebSocket vs Raw TCP

**Question:** Why use WebSockets instead of raw TCP sockets?

**Answer:**

1. **Browser compatibility**: Web browsers can only use WebSocket (or HTTP), not raw TCP sockets. Since our frontend runs in a browser, WebSocket is the only viable option for real-time bidirectional communication.

2. **Framing**: WebSocket provides **message-level framing**. When you send a message, the receiver gets the complete message. Raw TCP is a **byte stream** — you'd need to implement your own length-prefixed framing protocol.

3. **Firewall traversal**: WebSocket starts as an HTTP request on port 80/443, passing through corporate firewalls and proxies. Raw TCP on port 8080 may be blocked.

4. **Protocol overhead**: Minimal. The WebSocket frame header is 2-14 bytes. The handshake is a one-time HTTP upgrade. After that, it's essentially raw TCP with framing.

5. **Full-duplex**: Like TCP, WebSocket supports simultaneous sending and receiving. Unlike HTTP long-polling or SSE, both sides can send at any time.

**Our handshake process (RFC 6455):**
```
Client → Server: HTTP GET with Upgrade: websocket, Sec-WebSocket-Key
Server → Client: HTTP 101 with Sec-WebSocket-Accept (SHA-1 + Base64)
After handshake: Binary WebSocket frames over the same TCP connection
```

---

### 4. Socket Options Impact

**Question:** Explain the impact of the socket options we configured.

**Answer:**

#### `TCP_NODELAY` — **Latency impact**
- **Without**: Nagle's algorithm buffers small writes (< MSS ≈ 1460 bytes), waiting up to 200ms to combine them into one packet
- **With**: Each `send()` immediately sends data as a separate TCP segment
- **Our use case**: Location updates are ~100 bytes each. Without `TCP_NODELAY`, users would see 200ms+ delay in location updates. With it, updates appear in <5ms
- **Trade-off**: Slightly more packets on the network (more TCP/IP headers), but negligible for our data volume

#### `SO_KEEPALIVE` — **Connection robustness**
- **Purpose**: Detects dead connections (half-open sockets) where the peer crashed without sending FIN
- **Mechanism**: After idle timeout (default ~2 hours on Windows), OS sends TCP keep-alive probes. No response after several probes → connection is dead
- **Our use case**: Mobile users entering tunnels or airplane mode. Without keepalive, these sockets leak indefinitely in our `clients[]` array
- **Can tune**: Windows registry keys `KeepAliveTime` and `KeepAliveInterval` control timing

#### `SO_REUSEADDR` — **Server restart**
- **Purpose**: Allows `bind()` to succeed even if the port has sockets in TIME_WAIT
- **Without**: After stopping the server, you must wait ~60 seconds before restarting
- **With**: Instant restart. Critical during development and after crashes

#### `SO_RCVBUF = 128KB` — **Throughput under load**
- **Default**: ~8KB on Windows
- **Increased to**: 128KB
- **Purpose**: During load tests with 50+ clients sending every 200ms, the kernel receive buffer can fill up. TCP would then advertise a zero window, causing the sender to pause. A larger buffer absorbs bursts without flow control intervention
- **Trade-off**: More memory per socket (128KB × 30 clients = ~3.75MB)

---

### 5. Handling Partial `recv()`

**Question:** How does partial `recv()` occur and how do we handle it?

**Answer:**

A partial `recv()` happens when `recv()` returns fewer bytes than requested. This is **normal** in TCP because:
- TCP is a **byte stream** — it has no concept of message boundaries
- Data arrives in **TCP segments** that may not align with application messages
- The kernel buffer may have less data than requested

**Our handling:**

1. **`robust_send()` in `network.c`**: Loops `send()` until all bytes are sent:
   ```c
   while (total < len) {
       int n = send(sockfd, buf + total, len - total, 0);
       // handle WSAEWOULDBLOCK with Sleep(1) + retry
       total += n;
   }
   ```

2. **`websocket_frame_recv()` in `websocket.c`**: Uses a two-phase approach:
   - **Phase 1**: Read 2-byte header to learn payload length
   - **Phase 2**: Loop `robust_recv()` until exactly `payload_len` bytes are read:
   ```c
   while (total_read < to_read) {
       int n = robust_recv(client_sock, buffer + total_read, to_read - total_read);
       if (n <= 0) return -1;
       total_read += n;
   }
   ```
   This guarantees complete WebSocket frames are always processed.

3. **WebSocket handshake**: The initial HTTP request is typically small enough (~200 bytes) to arrive in one recv(). Our buffer is 4096 bytes, accommodating even large headers.

---

### 6. TLS / Secure Communication

**Question:** How would TLS be integrated for secure communication?

**Answer:**

Our current system uses unencrypted WebSocket (`ws://`). For production TLS (`wss://`):

**Architecture with TLS:**
```
Client ←→ [TLS/SSL Layer] ←→ [WebSocket] ←→ [TCP]
```

**Implementation options on Windows:**
1. **OpenSSL**: Create an SSL context, load certificates, wrap the socket with `SSL_new()`. Each `send()`/`recv()` becomes `SSL_write()`/`SSL_read()`.
2. **Windows Schannel**: Native Windows TLS via `InitializeSecurityContext()` and `EncryptMessage()` / `DecryptMessage()`.
3. **Reverse proxy**: Place nginx or Caddy in front of our server. The proxy handles TLS termination and forwards unencrypted WebSocket to our C server.

**TLS Handshake flow:**
```
Client → ClientHello (supported cipher suites)
Server → ServerHello + Certificate + ServerHelloDone
Client → ClientKeyExchange + ChangeCipherSpec + Finished
Server → ChangeCipherSpec + Finished
--- Encrypted channel established ---
WebSocket Upgrade request over this encrypted channel
```

**Why we didn't implement TLS:**
- OpenSSL/Schannel significantly increase code complexity
- Certificate management adds operational overhead
- For an academic demo on localhost, TLS adds complexity without functional benefit
- The core networking concepts (TCP, select, socket options) are identical with or without TLS

---

### 7. Database Design and Consistency

**Question:** How do we ensure database consistency under continuous updates?

**Answer:**

- **ACID compliance**: SQLite provides full ACID transactions. Each INSERT is atomic.
- **WAL mode**: Write-Ahead Logging allows concurrent reads while writes are happening. Readers see a consistent snapshot.
- **PRAGMA synchronous=NORMAL**: Balances durability with write performance. Data survives application crashes (OS handles fsync).
- **Timestamping**: All tables use `DEFAULT CURRENT_TIMESTAMP` for automatic, consistent timestamps.
- **Parameterized queries**: All SQL uses `sqlite3_bind_*()`, preventing SQL injection.
- **Connection management**: Single database connection used by the single-threaded server (no concurrency issues).

---

### 8. WinSock2 vs POSIX Sockets

**Question:** What are the key differences between WinSock2 and POSIX sockets?

**Answer:**

| Aspect | WinSock2 (Windows) | POSIX (Linux/macOS) |
|--------|-------------------|---------------------|
| **Init** | `WSAStartup()` required | None needed |
| **Socket type** | `SOCKET` (unsigned) | `int` (file descriptor) |
| **Invalid socket** | `INVALID_SOCKET` | `-1` |
| **Close** | `closesocket()` | `close()` |
| **Error codes** | `WSAGetLastError()` returns `WSAExxxx` | `errno` contains `Exxxx` |
| **Non-blocking** | `ioctlsocket(FIONBIO)` | `fcntl(F_SETFL, O_NONBLOCK)` |
| **select() nfds** | Ignored (pass 0) | Must be `max_fd + 1` |
| **setsockopt optval** | `const char*` | `const void*` |
| **Byte order** | Same `htons()`, `ntohs()` | Same functions |
| **Cleanup** | `WSACleanup()` | None needed |

---

### 9. Concurrency Model and Data Integrity

**Question:** How does the single-threaded select() model maintain data integrity?

**Answer:**

Data integrity is **inherently guaranteed** by the single-threaded nature:

1. **No race conditions**: Only one thread accesses the `clients[]` array. No two operations can simultaneously modify the same client slot.
2. **Atomic message processing**: Each incoming message is fully processed (parsed, stored in DB, and broadcast) before the next message is handled.
3. **Sequential broadcast**: When broadcasting to N clients, each `send()` completes before the next. No interleaving of partial messages.
4. **Database consistency**: Single-threaded access to SQLite means no concurrent transaction conflicts.

**Potential issue**: If a `send()` call blocks (slow client), all other clients are delayed. Mitigation: non-blocking sockets + `WSAEWOULDBLOCK` handling with brief sleep.
