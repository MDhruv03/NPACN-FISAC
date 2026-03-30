# Failure Analysis

This document outlines potential failure scenarios for the real-time location sharing application and describes the mitigation strategies implemented.

## System Description

The system consists of:
- A C-based WebSocket server using WinSock2 with `select()` I/O multiplexing
- Embedded SQLite3 database for persistent storage
- WebSocket clients (web browsers) that send location data
- Leaflet.js frontend for real-time map visualization

---

## Failure Scenarios

### 1. Client Crash (Sudden Disconnection)

*   **Scenario**: A client application crashes or the underlying OS terminates it. The TCP connection is not gracefully closed — no FIN packet is sent.
*   **TCP State**: The server-side socket remains in ESTABLISHED but the peer is gone. This is a "half-open" connection.
*   **Mitigation**:
    *   **`SO_KEEPALIVE`**: Enabled on the server socket. The Windows TCP stack sends periodic keep-alive probes after the idle timeout. If no response is received after the configured number of probes, the connection is marked dead and `select()` will signal the socket as readable with a recv() returning 0.
    *   **`select()` detection**: When `select()` flags a dead socket and `recv()` returns 0 or `WSAECONNRESET`, the server calls `closesocket()` and clears the client slot.
    *   **Code**: `server.c` lines in the client data handling section check for `len <= 0` and clean up the slot.

### 2. Network Congestion

*   **Scenario**: The network between client and server becomes congested, leading to packet loss and delays.
*   **TCP State**: The TCP window size shrinks (zero window), triggering TCP flow control. Packets are retransmitted.
*   **Mitigation**:
    *   **`SO_RCVBUF = 128KB`**: Increased from the default 8KB to absorb traffic bursts. During load tests with many concurrent clients, the extra buffer prevents TCP from advertising a zero window.
    *   **`robust_send()`**: Handles `WSAEWOULDBLOCK` by retrying with a small sleep, accommodating temporary send buffer fullness.
    *   **`robust_recv()`**: Returns `-1` on `WSAEWOULDBLOCK` (non-blocking), allowing the `select()` loop to continue processing other clients without blocking.
    *   **Client-side**: The WebSocket API handles TCP-level retransmission transparently.

### 3. Partial Transmission

*   **Scenario**: `send()` or `recv()` returns fewer bytes than requested. This is normal TCP behavior — TCP is a stream protocol with no inherent message boundaries.
*   **Mitigation**:
    *   **`robust_send()`** in `network.c`: Loops until all `len` bytes are transmitted, handling `WSAEWOULDBLOCK` with a retry.
    *   **`websocket_frame_recv()`** in `websocket.c`: First reads the 2-byte frame header to determine payload length, then loops recv() until the exact payload length is received. This ensures complete WebSocket frames are always processed.
    *   **`robust_recv()`**: Single `recv()` call with error code classification. The WebSocket layer calls it repeatedly as needed.

### 4. Unauthorized Access Attempts

*   **Scenario**: An unauthenticated or malicious user attempts to send location data or other privileged messages.
*   **Mitigation**:
    *   **Per-client auth state**: Each `ClientInfo` struct tracks an `authenticated` flag. Initially 0.
    *   **Auth-gated message processing**: In `protocol.c`, `handle_message()` only allows `auth` and `register` messages from unauthenticated clients. All other message types receive an `error` response.
    *   **Password hashing**: Passwords are hashed (djb2) before storage. Plaintext passwords are never stored.
    *   **Auth failure logging**: Failed login attempts are logged to the database for audit.

### 5. Database Unavailability

*   **Scenario**: SQLite3 encounters an error (disk full, corruption, locked).
*   **Mitigation**:
    *   **WAL mode**: SQLite is configured with Write-Ahead Logging for better concurrency. Reads don't block writes.
    *   **Error checking**: All `sqlite3_step()` calls check return codes and log errors.
    *   **Non-fatal failures**: Database write failures (e.g., location inserts) are logged but don't crash the server. The WebSocket relay continues operating.
    *   **Atomic transactions**: SQLite3 provides ACID compliance, ensuring database consistency even after abnormal shutdown.

### 6. High Update Frequency (DoS-like behavior)

*   **Scenario**: A client sends location updates at an extremely high rate (100s per second), overwhelming the server.
*   **Mitigation**:
    *   **Non-blocking I/O**: The `select()`-based architecture processes messages as fast as possible without blocking other clients.
    *   **SQLite batching**: WAL mode allows concurrent writes without locking the database.
    *   **`SO_RCVBUF`**: The 128KB receive buffer absorbs burst traffic without dropping data at the TCP level.
    *   **Future improvement**: Rate limiting per client (e.g., max 10 updates/second) could be added.

### 7. Server Crash / Restart

*   **Scenario**: The server process crashes or is restarted.
*   **TCP State**: All server-side sockets close. Clients see connection reset (WSAECONNRESET). Server-side ports enter TIME_WAIT for 2*MSL (~60s).
*   **Mitigation**:
    *   **`SO_REUSEADDR`**: Enabled on the server socket. This allows the server to bind to port 8080 immediately after restart, even if sockets from the previous instance are still in TIME_WAIT.
    *   **SQLite persistence**: All previously stored data survives the crash. The database file (`fisac.db`) is intact due to WAL journaling.
    *   **Client reconnection**: The frontend can be refreshed to reconnect.
    *   **Signal handler**: `signal(SIGINT, ...)` in `main.c` handles Ctrl+C for graceful shutdown with proper resource cleanup.
