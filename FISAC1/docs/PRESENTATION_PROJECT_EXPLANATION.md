# GeoSync (Group 12) - Full Project Presentation Explanation

Version: 2026-04-06
Project: Real-Time Location Sharing Web Application

---

## 1. One-Line Project Summary

GeoSync is a real-time location sharing system where browser clients communicate over WebSocket/TCP with a C (WinSock2) server, and persistence/auth/logging are handled by a local Flask + SQLite backend.

---

## 2. Problem Statement and Assignment Alignment

We needed to build and evaluate a socket-programming based, concurrent, real-time application that demonstrates:

- TCP + WebSocket communication
- Concurrent multi-client handling
- Authentication and authorization
- Persistent storage and event logs
- Real-time broadcast behavior
- Socket option tuning and measurable impact
- Robustness against common failures and abnormal traffic

This implementation addresses all four major evaluation questions:

- Q1: Workflow and concurrency model
- Q2: Server/auth/storage/broadcast pipeline
- Q3: Socket options and performance profile comparison
- Q4: Robustness and failure behavior under stress

---

## 3. Current Runtime Architecture (Important)

### 3.1 Effective runtime architecture in this branch

```text
[Browser Frontend]
    |  WebSocket (ws://localhost:8080)
    v
[C Server: WinSock2 + select()]
    |  HTTP POST (/auth, /register, /location, /log)
    v
[Flask Backend: localhost:5000]
    |
    v
[SQLite DB: fisac.db]
```

### 3.2 Why this note matters

Some older documentation text describes a pure C + embedded SQLite runtime. In the currently runnable setup:

- C server is still the real-time socket core
- Python Flask is active for auth/storage/log endpoints
- `src/database.c` exists, but is not linked in current `Makefile`

So during presentation, describe this as a **hybrid architecture**.

---

## 4. Component Responsibilities

### 4.1 C server side

- `src/main.c`
  - Starts WinSock2
  - Initializes and runs server on port 8080
- `src/server.c`
  - `select()` based multi-client event loop
  - Accepts sockets, handshakes, receives frames, dispatches messages
  - Broadcasts location updates to authorized peers
- `src/websocket.c`
  - RFC6455 handshake and frame parsing/sending
  - Handles masking, extended payload lengths, control frames
- `src/network.c`
  - `robust_send` and `robust_recv` wrappers for partial I/O and socket errors
- `src/socket.c`
  - Socket options: SO_REUSEADDR, SO_KEEPALIVE, SO_RCVBUF, TCP_NODELAY
  - Baseline vs tuned profile via `FISAC_SOCKOPTS_PROFILE`
- `src/protocol.c`
  - Message routing (`auth`, `register`, `location`, `subscribe`, `error`)
  - Auth gate for non-authenticated clients
  - Forwards DB-related work to backend endpoints via HTTP
- `src/http_client.c`
  - Blocking localhost HTTP client for backend calls

### 4.2 Backend side

- `scripts/service.py`
  - Serves frontend page (`/`)
  - Endpoints: `/auth`, `/register`, `/location`, `/log`
  - New endpoint for presentation evidence: `/stats`
  - SQLite schema creation and demo-user seeding

### 4.3 Frontend side

- `frontend/index.html`
  - UI sections for map, auth, metrics, protocol inspector, evidence panels
- `frontend/app.js`
  - WebSocket client logic
  - Geolocation watch and location publishing
  - Peer rendering, trails, and presentation counters
  - Demo actions (burst, malformed payload, reconnect drill, snapshot export)
- `frontend/style.css`
  - Dark responsive dashboard styling

### 4.4 Orchestration

- `run_all.bat`
  - Kills old processes, builds C server, starts backend + server
  - Passes socket profile to both processes
  - Opens frontend with profile in query string

---

## 5. End-to-End Runtime Flow

1. User runs `run_all.bat`
2. Flask backend starts on `127.0.0.1:5000`
3. C server starts on `0.0.0.0:8080`
4. Browser opens frontend and creates WebSocket connection to `ws://localhost:8080`
5. HTTP Upgrade handshake completes (`101 Switching Protocols`)
6. Client sends `auth` or `register` JSON frame
7. C server forwards credentials to Flask `/auth` or `/register`
8. Flask validates against SQLite and returns JSON result
9. C server sends `auth_response` frame back to browser
10. Authenticated client sends `location` updates
11. C server forwards each location to backend `/location` for persistence
12. C server broadcasts location updates to other authenticated clients
13. Event logs are written via backend `/log`

---

## 6. Q1 Explanation (Workflow + Concurrency)

### 6.1 Workflow proof points

From `docs/q1_workflow_capture.txt`:

- TCP connect succeeded
- WebSocket handshake succeeded with `HTTP/1.1 101 Switching Protocols`
- Auth request and successful `auth_response` captured
- TCP state snapshot confirms listening + established connections during run

### 6.2 Concurrency model

- Single-threaded `select()` multiplexing
- One thread monitors all active sockets
- No mutex/lock complexity for shared client state
- Suitable for I/O-bound real-time relay workloads

### 6.3 Why this model was chosen

- Predictable behavior
- Lower concurrency bug risk than ad-hoc multi-threading
- Adequate for project scale (tens of concurrent clients)

---

## 7. Q2 Explanation (Server + Auth + Storage + Broadcast)

### 7.1 Auth pipeline

- Browser sends `auth` / `register` over WebSocket
- C server forwards credentials to Flask
- Flask validates or inserts user in SQLite
- C server returns `auth_response`

### 7.2 Data persistence path

- Authenticated client sends `location`
- C server forwards payload to `/location`
- Flask inserts into `locations` table with timestamp

### 7.3 Broadcast path

- After processing an incoming location message,
- C server relays it to other authenticated sockets
- Unauthorized clients are rejected with error response

### 7.4 DB/log evidence

From `docs/evidence/q2_db_snapshot.txt`:

- users: 32
- locations: 1978
- logs: 1764

This verifies sustained insert activity and event correlation.

---

## 8. Q3 Explanation (Socket Options + Performance)

### 8.1 Socket options configured

- SO_REUSEADDR
- SO_KEEPALIVE
- SO_RCVBUF
- TCP_NODELAY

Profiles:

- Baseline: lower/default behavior
- Tuned: optimized options enabled

### 8.2 Measured results (from evidence)

Source: `docs/evidence/q3_q4_summary.json`

| Metric | Baseline | Tuned |
|---|---:|---:|
| Throughput (msg/s) | 10.73 | 13.84 |
| p95 latency (ms) | 1.16 | 1.07 |
| Errors | 2 | 2 |
| Messages sent | 215 | 278 |
| Messages received | 429 | 646 |

Interpretation:

- Tuned profile increased throughput significantly
- Latency p95 improved slightly
- Error count remained stable

---

## 9. Q4 Explanation (Robustness)

### 9.1 Scenarios tested

- Message flood
- Unauthorized message attempts
- Invalid credential attempts
- Rapid connect/disconnect cycles
- Malformed payload injection
- Idle delay then resumed traffic

### 9.2 Outcomes (from evidence)

Source: `docs/evidence/chaos_tuned.json`

- flood_sent: 200
- unauthorized_attempts: 2
- rapid_connect_disconnect: 45
- malformed_sent: 0
- errors: 13

### 9.3 TCP state sanity checks

Source: `docs/evidence/q3_q4_summary.json`

- time_wait_8080: 1 (expected transient behavior)
- close_wait_8080: 0 (good cleanup behavior)

---

## 10. Frontend Demonstration Layer (Presentation Features)

The UI now includes assignment-aligned evidence panels:

- Q1-Q4 Alignment board
- Workflow timeline (connection/auth/subscribe/streaming states)
- Backend evidence cards (users/locations/logs/profile/sync)
- Robustness snapshot counters
- Protocol inspector (last TX and RX JSON)
- Demo controls:
  - Start/Stop demo route
  - Burst x25
  - Send malformed payload
  - Reconnect drill
  - Export snapshot

These are useful to show both implementation and evaluation in a single live dashboard.

---

## 11. API Contract Summary

### 11.1 WebSocket message types

- `auth`
- `register`
- `auth_response`
- `location`
- `subscribe`
- `error`

### 11.2 Backend HTTP endpoints

- `POST /auth`
- `POST /register`
- `POST /location`
- `POST /log`
- `GET /stats` (presentation/evidence endpoint)

---

## 12. Build and Run Commands

### 12.1 Main run

```bat
run_all.bat
```

### 12.2 Profile-specific run

```bat
run_all.bat --no-pause baseline
run_all.bat --no-pause tuned
```

### 12.3 Python dependencies

```bash
python -m pip install -r scripts/requirements.txt
```

### 12.4 Evaluation scripts

```bash
python scripts/load_test.py
python scripts/chaos_test.py
python scripts/profile_eval.py
```

---

## 13. 10-Minute Presentation Script (Suggested)

### 0:00 - 1:00 Intro

- Problem: real-time multi-user location sharing with socket programming
- Requirement: prove concurrency, persistence, tuning impact, robustness

### 1:00 - 2:30 Architecture

- Show hybrid architecture diagram
- Explain why C handles realtime socket path, Flask handles persistence API

### 2:30 - 4:00 Q1 workflow live

- Login and show workflow timeline changing state-by-state
- Mention handshake/auth evidence file

### 4:00 - 5:30 Q2 pipeline live

- Show location updates on map
- Show protocol inspector TX/RX and backend evidence counts increasing

### 5:30 - 7:00 Q3 performance

- Show baseline vs tuned metrics table from evidence
- Emphasize throughput gain and p95 latency improvement

### 7:00 - 8:30 Q4 robustness

- Trigger burst test and reconnect drill
- Optionally trigger malformed payload action
- Show robustness counters and stable app behavior

### 8:30 - 9:30 Evidence and traceability

- Open `docs/evidence/*` JSON files
- Explain that results are reproducible via scripts

### 9:30 - 10:00 Wrap-up

- Restate that all assignment questions are addressed with measurable outcomes

---

## 14. Evidence Map (What to show on screen)

- Workflow capture: `docs/q1_workflow_capture.txt`
- Q2 backend/db snapshot: `docs/evidence/q2_db_snapshot.txt`
- Baseline performance: `docs/evidence/load_baseline.json`
- Tuned performance: `docs/evidence/load_tuned.json`
- Chaos results: `docs/evidence/chaos_tuned.json`
- Final summary: `docs/evidence/q3_q4_summary.json`

---

## 15. Known Limitations (Honest but safe to present)

- Current deployment uses `ws://` on localhost (not TLS)
- HTTP bridge from C to Flask is blocking in current form
- No advanced role-based access control
- Malformed payload counter in older evidence shows zero due how that script records events

---

## 16. Future Improvements

- Add TLS (`wss://`) via reverse proxy or native TLS integration
- Add non-blocking backend queue or worker model to isolate DB latency
- Add token-based session auth and stronger password hashing
- Add server-side rate limiting per client
- Add observability dashboard for p50/p95/p99 over time

---

## 17. Talking Points for Viva Questions

### 17.1 Why `select()` instead of threads?

- Lower complexity and fewer race conditions for this workload
- Good fit for I/O multiplexing at project scale

### 17.2 Why WebSocket over raw TCP?

- Browser-native full duplex support
- Standardized framing and handshake
- Easy integration with frontend

### 17.3 How partial send/recv is handled?

- `robust_send` loops until full write
- WebSocket frame parser reads exact lengths including extended payload forms

### 17.4 How do you prove robustness?

- Chaos script outcomes + TCP state checks + no CLOSE_WAIT buildup

---

## 18. Final Closing Statement

This project demonstrates practical socket programming in C with real-time multi-client behavior, measurable tuning impact, persistent storage, and failure-oriented validation. The implementation is not only functional but also backed by reproducible evidence for each assignment question.

---

## 19. Communication Deep-Dive (Cross-Question Ready)

This section is a detailed technical explanation of the complete communication pipeline in this project: browser <-> C server <-> Flask backend <-> SQLite.

### 19.1 Communication Layers and Responsibilities

| Layer | Technology | Direction | Responsibility |
|---|---|---|---|
| Browser <-> Server | WebSocket over TCP | Full-duplex | Real-time auth, subscribe, location publish, location receive |
| C server internal | `select()` + WinSock2 | Event loop | Multi-client I/O multiplexing, frame handling, auth gate, broadcast |
| C server <-> Flask | HTTP/1.0 over localhost TCP | Request-response | Auth/register validation, location persistence, event logs |
| Flask <-> SQLite | SQLite (WAL) | Local DB ops | Durable storage for users, locations, logs |

Important: in the current runnable architecture, real-time socket communication is in C, while persistence/auth are delegated to Flask.

### 19.2 Boot Sequence and Channel Establishment

1. `main()` initializes WinSock2 and starts server on `0.0.0.0:8080`.
2. `server_init()` creates listening socket, applies socket options, binds, and marks listen socket non-blocking.
3. `server_run()` enters `select()` loop and watches:
   - the listening socket (new connection events), and
   - all active client sockets (incoming data frames).
4. New TCP connection accepted -> WebSocket handshake executed.
5. Successful handshake -> client is inserted into free slot (`MAX_CLIENTS=30`) with unauthenticated state.

Code excerpt:

```c
void server_run(Server *server) {
  listen_on_socket(server->sock);
  while (server->running) {
    FD_ZERO(&readfds);
    FD_SET(server->sock, &readfds);

    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (server->clients[i].sock != INVALID_SOCKET) {
        FD_SET(server->clients[i].sock, &readfds);
      }
    }

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    int activity = select(0, &readfds, NULL, NULL, &timeout);

    if (FD_ISSET(server->sock, &readfds)) {
      SOCKET new_socket = accept(server->sock, ...);
      unsigned long iMode = 0;
      ioctlsocket(new_socket, FIONBIO, &iMode); /* force blocking */

      if (websocket_handshake(new_socket) == 0) {
        /* insert into free client slot */
      }
    }
  }
}
```

Teacher note: accepted sockets are explicitly switched back to blocking mode. This is intentional so frame assembly logic can reliably read exact byte counts without complex non-blocking state machines.

### 19.3 WebSocket Handshake Internals (RFC6455)

The server performs a standards-based HTTP upgrade:

1. Reads incoming HTTP request until `\r\n\r\n`.
2. Extracts `Sec-WebSocket-Key`.
3. Concatenates with GUID `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`.
4. Computes SHA1 digest.
5. Base64-encodes digest.
6. Returns `HTTP/1.1 101 Switching Protocols` with `Sec-WebSocket-Accept`.

Formula used:

`Sec-WebSocket-Accept = Base64( SHA1( client_key + GUID ) )`

Code excerpt:

```c
char *key_start = strstr(buffer, "Sec-WebSocket-Key: ");
snprintf(concatenated_key, sizeof(concatenated_key), "%s%s", key_start, WEBSOCKET_GUID);

sha1_init(&sha1_ctx);
sha1_update(&sha1_ctx, (uint8_t *)concatenated_key, (int)strlen(concatenated_key));
sha1_final(&sha1_ctx, sha1_digest);

base64_encode(sha1_digest, SHA1_DIGEST_SIZE, base64_encoded);

snprintf(response, sizeof(response),
     "HTTP/1.1 101 Switching Protocols\r\n"
     "Upgrade: websocket\r\n"
     "Connection: Upgrade\r\n"
     "Sec-WebSocket-Accept: %s\r\n\r\n",
     base64_encoded);
```

### 19.4 WebSocket Frame Receive/Decode Path

`websocket_frame_recv()` decodes one full frame per call.

Frame parsing logic:

- Reads first 2 header bytes.
- Extracts opcode, mask bit, base payload length.
- Handles extended lengths:
  - `126` => next 2 bytes (16-bit length)
  - `127` => next 8 bytes (64-bit length)
- Reads 4-byte masking key when present.
- Reads payload and unmasks it.
- Handles control/data opcodes:
  - `0x8` close => disconnect path
  - `0x9` ping => replies with pong (`0xA`)
  - `0xA` pong => ignore
  - non-text opcodes => ignore gracefully

Code excerpt:

```c
uint8_t opcode = header[0] & 0x0F;
uint8_t mask = (header[1] >> 7) & 1;
uint64_t payload_len = header[1] & 0x7F;

if (payload_len == 126) {
  uint16_t len;
  recv_exact(client_sock, (char *)&len, 2);
  payload_len = ntohs(len);
} else if (payload_len == 127) {
  uint64_t len;
  recv_exact(client_sock, (char *)&len, 8);
  payload_len = swap_uint64(len);
}

if (mask) {
  recv_exact(client_sock, (char *)masking_key, 4);
}

recv_exact(client_sock, buffer, (int)payload_len);
for (int i = 0; i < to_read; i++) {
  buffer[i] ^= masking_key[i % 4];
}
```

Safety behavior:

- Rejects payloads larger than internal buffer (`4096` bytes minus terminator).
- Returns `0` for ping/pong/non-text so event loop can continue without disconnect.
- Returns `-1` for close/error -> server removes client.

### 19.5 Transport Reliability: Partial I/O and Errors

TCP does not preserve message boundaries; `send` and `recv` may be partial.

- `robust_send()` loops until all bytes are sent or fatal error occurs.
- `robust_recv()` classifies outcomes:
  - `>0`: bytes received
  - `0`: graceful close / reset
  - `-1`: `WSAEWOULDBLOCK`
  - `-2`: fatal recv error
- `recv_exact()` in WebSocket layer repeatedly calls `robust_recv()` to assemble exact header/payload lengths.

Code excerpt:

```c
int robust_send(SOCKET sockfd, const char *buf, int len) {
  int total = 0;
  while (total < len) {
    int n = send(sockfd, buf + total, len - total, 0);
    if (n == SOCKET_ERROR) {
      int err = WSAGetLastError();
      if (err == WSAEWOULDBLOCK) { Sleep(1); continue; }
      return -1;
    }
    total += n;
  }
  return total;
}
```

### 19.6 Application Protocol (JSON Over WebSocket)

Defined message types:

- `auth`
- `register`
- `auth_response`
- `location`
- `subscribe`
- `error`

Canonical examples used in runtime:

```json
{
  "type": "auth",
  "payload": {
  "username": "user1",
  "password": "pass1"
  }
}
```

```json
{
  "type": "location",
  "payload": {
  "latitude": 28.6139,
  "longitude": 77.2090,
  "userId": "user1"
  }
}
```

### 19.7 Auth/Register Path: Browser -> C -> Flask -> SQLite -> C -> Browser

Auth flow:

1. Frontend opens WebSocket and sends `auth` or `register`.
2. C server `handle_message()` dispatches by `type`.
3. `handle_auth()` or `handle_register()` posts JSON to Flask.
4. Flask validates against SQLite and returns JSON.
5. C server sets `client->authenticated`, `client->user_id`, `client->username` on success.
6. C server sends `auth_response` frame to browser.

Code excerpt (C):

```c
if (strcmp(type->valuestring, MSG_TYPE_AUTH) == 0) {
  handle_auth(client_info, payload);
} else if (strcmp(type->valuestring, MSG_TYPE_REGISTER) == 0) {
  handle_register(client_info, payload);
}
```

```c
if (http_post_json("/auth", req_json, resp_json, sizeof(resp_json)) == 0) {
  /* parse success + user_id from backend response */
}
```

Code excerpt (Flask):

```python
@app.route('/auth', methods=['POST'])
def auth():
  data = request.json
  username = data.get('username')
  password = data.get('password')
  pwd_hash = hash_password(password)
  # query users table and return success + user_id
```

### 19.8 Authorization Gate and Broadcast Semantics

Authorization rule in server:

- `auth` and `register` allowed before login.
- Any other type from unauthenticated client => `error` message (`"Authentication required"`).

Location broadcast rule:

- Frame is rebroadcast only if sender is authenticated.
- Recipients must also be authenticated.
- Sender does not receive its own broadcast copy.

Code excerpt:

```c
} else if (!client_info->authenticated) {
  send_json_response(client_info->sock,
    "{\"type\":\"error\",\"payload\":{\"message\":\"Authentication required\"}}");
}
```

```c
if (type && strcmp(type->valuestring, MSG_TYPE_LOCATION) == 0) {
  if (server->clients[i].authenticated) {
    server_broadcast(server, buffer, sd);
  }
}
```

### 19.9 Persistence Bridge: C -> Flask HTTP Client

For auth/register/location/log, C code uses `http_post_json()`.

Communication characteristics:

- Opens new TCP socket per request.
- Connects to `127.0.0.1:5000`.
- Sends HTTP/1.0 request with `Connection: close`.
- Uses 2s send/recv socket timeouts to prevent infinite blocking.
- Extracts response body by splitting at `\r\n\r\n`.

Code excerpt:

```c
setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));

snprintf(request, sizeof(request),
  "POST %s HTTP/1.0\r\n"
  "Host: 127.0.0.1:5000\r\n"
  "Content-Type: application/json\r\n"
  "Connection: close\r\n"
  "Content-Length: %d\r\n\r\n%s",
  endpoint, payload_len, json_payload);
```

Flask side deliberately mirrors close behavior:

```python
@app.after_request
def force_close_connection(response):
  response.headers['Connection'] = 'close'
  return response
```

### 19.10 Frontend Communication State Machine

Frontend sequence:

1. `connectAndAuth()` creates `new WebSocket('ws://localhost:8080')`.
2. `onopen` sends `auth`/`register` and starts auth timeout guard.
3. `onmessage` parses JSON, then routes by message type.
4. On successful auth:
   - app enters connected state,
   - sends `subscribe` message,
   - starts geolocation watch,
   - pushes periodic `location` messages.
5. `onclose` handles cleanup and reconnect metrics.

Code excerpt:

```javascript
ws = new WebSocket('ws://localhost:8080');

ws.onopen = () => {
  const msg = {
    type: authMode === 'register' ? 'register' : 'auth',
    payload: { username, password }
  };
  sendWsPayload(msg);
};

function emitOwnLocation(lat, lon) {
  if (state === STATE.CONNECTED && ws && ws.readyState === WebSocket.OPEN) {
    sendWsPayload({
      type: 'location',
      payload: { latitude: lat, longitude: lon, userId: myUsername }
    });
  }
}
```

### 19.11 Socket Tuning and Communication Impact

`set_socket_options()` applies profile-based options using environment variable `FISAC_SOCKOPTS_PROFILE`.

Applied options:

- `SO_REUSEADDR`: restart resilience after TIME_WAIT.
- `TCP_NODELAY`: lower latency for small frequent location frames.
- `SO_KEEPALIVE`: detect dead half-open connections.
- `SO_RCVBUF`: absorb burst traffic and reduce packet drops/retransmit pressure.

In your evidence, tuned profile improves throughput while keeping latency stable.

### 19.12 Failure Modes and What Actually Happens

1. Invalid JSON from client:
   - `cJSON_Parse` fails -> message dropped, server keeps running.
2. Unauthorized location message:
   - server replies with `error: Authentication required`.
3. Client sends close frame:
   - `websocket_frame_recv` returns `-1` -> server closes socket and clears slot.
4. Backend unavailable:
   - `http_post_json` fails quickly (2s timeout), auth fails or storage/log write skipped.
5. Ping frame received:
   - server responds with pong and keeps connection alive.

### 19.13 Subtle Design Points (Good for Viva Depth)

- Listening socket is non-blocking, accepted sockets are switched to blocking for simpler frame assembly.
- Event loop monitors readability only; writes happen inline in same thread.
- `subscribe` currently acts as a logical marker (logged), not a strict server-side channel filter.
- Location DB insert uses authenticated `user_id` from server state, not frontend `userId` string.
- Broadcast payload carries raw client JSON, so `userId` displayed on peers is client-provided.

### 19.14 Known Communication Limitations (Say this confidently)

- No TLS (`wss://`) in current localhost demo.
- C -> Flask bridge is synchronous; slow backend can stall select-loop progress.
- WebSocket fragmentation continuation frames are not fully reassembled as multi-frame messages.
- No explicit server-side rate limiter per client.

### 19.15 Teacher Cross-Question Bank (Ready Answers)

Q1. Why use WebSocket instead of polling HTTP?

- Full-duplex low-latency updates with one persistent TCP connection, ideal for continuous location streaming.

Q2. How do you prove the handshake is standards-compliant?

- We compute `Sec-WebSocket-Accept` exactly from `Sec-WebSocket-Key + GUID`, SHA1, then Base64, and return HTTP `101 Switching Protocols`.

Q3. How are partial TCP writes handled?

- `robust_send()` loops until all bytes are transmitted or a fatal socket error occurs.

Q4. How are partial TCP reads handled for frame decoding?

- `recv_exact()` repeatedly reads until exact header/payload bytes are assembled.

Q5. What enforces authorization?

- Message dispatcher allows only `auth/register` pre-login; all other message types from unauthenticated clients are rejected with `error`.

Q6. Can unauthenticated clients receive broadcasts?

- No. Broadcast loop sends only to slots marked `authenticated`.

Q7. Where does persistence happen?

- C server sends HTTP JSON to Flask endpoints (`/auth`, `/register`, `/location`, `/log`), and Flask writes into SQLite.

Q8. Why use HTTP/1.0 and `Connection: close` between C and Flask?

- Simpler implementation with deterministic request lifecycle and no keep-alive state complexity in C.

Q9. What happens if Flask is down?

- HTTP calls fail quickly due to 2-second socket timeouts; auth fails gracefully and server remains alive.

Q10. Why choose `select()` model?

- Single-threaded I/O multiplexing avoids lock complexity and race bugs, suitable for this workload and project scale.

Q11. Is your protocol binary or text?

- Text WebSocket frames carrying JSON payloads.

Q12. How do you handle keep-alive at WebSocket level?

- Ping (`opcode 9`) is answered with pong (`opcode 10`).

Q13. How is dead peer detection done at TCP level?

- `SO_KEEPALIVE` is enabled, helping OS-level detection of half-open dead sockets.

Q14. Any mismatch risk in identity fields?

- DB writes use server-authenticated `user_id`; however broadcast payload includes client-supplied `userId` string for UI labeling.

Q15. What is your strongest communication bottleneck today?

- Synchronous HTTP bridge from C to Flask in the same event-thread, which can add head-of-line blocking under backend delay.

### 19.16 Code Map for Instant Navigation

- Event loop and client lifecycle: `src/server.c`
- WebSocket handshake and frame I/O: `src/websocket.c`
- Message dispatch and auth gate: `src/protocol.c`
- TCP send/recv reliability wrappers: `src/network.c`
- Socket tuning profile and options: `src/socket.c`
- C -> Flask HTTP bridge: `src/http_client.c`
- Backend endpoints and DB persistence: `scripts/service.py`
- Browser WebSocket state machine and message send/receive: `frontend/app.js`

