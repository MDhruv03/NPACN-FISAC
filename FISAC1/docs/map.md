# Group 12 - Outcome Defense Map (Markdown)

## Project Prompt
Design and implement a secure real-time Location Sharing Web Application where authenticated users broadcast live coordinates to a central server, and the server instantly distributes updates to authorized connected clients using WebSockets over TCP.

## Required Outcomes
1. Analyze complete TCP + WebSocket workflow: secure setup, handshake, full-duplex exchange, graceful termination, and concurrency model justification.
2. Implement concurrent authenticated server with continuous location ingestion, persistent timestamped storage, syslog events, and efficient broadcast while handling partial transmissions and latency.
3. Configure and evaluate `SO_REUSEADDR`, `SO_KEEPALIVE`, `SO_RCVBUF`, `TCP_NODELAY`; analyze latency, throughput, session behavior, and write performance.
4. Evaluate robustness under disconnects, high-frequency updates, congestion, and unauthorized attempts; analyze `TIME_WAIT` / `CLOSE_WAIT` behavior, log correctness, and DB consistency.

## Implemented Runtime Architecture
```text
[Frontend Browser]
    <-> WebSocket (TCP 8080)
[C Server: select()-based concurrent loop]
    <-> HTTP loopback (127.0.0.1:5000)
[Python Service Layer]
    <-> SQLite persistent storage
```

---

## Outcome 1: TCP + WebSocket Workflow + Concurrency Model

### What Is Implemented
- TCP accept path and WebSocket upgrade in C event loop.
- RFC6455 handshake (`Sec-WebSocket-Key` -> SHA1 -> Base64 -> `Sec-WebSocket-Accept`).
- Full-duplex JSON frame exchange (`auth`, `register`, `location`, `subscribe`, `error`).
- Graceful close handling (close frame/socket cleanup).
- Single-threaded `select()` multiplexing for concurrent clients.

### Code Paths
- `src/server.c`: `server_run()`
- `src/websocket.c`: `websocket_handshake()`
- `src/websocket.c`: `websocket_frame_recv()`, `websocket_frame_send()`

### Short Code Evidence
```c
/* src/server.c */
int activity = select(0, &readfds, NULL, NULL, &timeout);
if (FD_ISSET(server->sock, &readfds)) {
    SOCKET new_socket = accept(server->sock, ...);
    if (websocket_handshake(new_socket) == 0) {
        server->clients[i].sock = new_socket;
    }
}
```

```c
/* src/websocket.c */
snprintf(concatenated_key, sizeof(concatenated_key), "%s%s", key_start, WEBSOCKET_GUID);
sha1_update(&sha1_ctx, (uint8_t *)concatenated_key, (int)strlen(concatenated_key));
base64_encode(sha1_digest, SHA1_DIGEST_SIZE, base64_encoded);
```

### Why This Satisfies Outcome 1
- Demonstrates the complete transport lifecycle: connect -> upgrade -> data exchange -> terminate.
- Concurrency model is justified for I/O-bound workload with predictable behavior.

### Evidence to Show
- `docs/q1_workflow_capture.txt`
- `docs/evidence/q4_netstat_8080.txt`

---

## Outcome 2: Concurrent Authenticated Server + Persistence + Syslog + Broadcast

### What Is Implemented
- Auth/register over WebSocket.
- Auth gate blocks non-auth traffic before login.
- Continuous location ingestion to SQLite via backend `/location`.
- Syslog-style event inserts via `/log`.
- Authenticated broadcast fanout to authorized peers.

### Code Paths
- `src/protocol.c`: `handle_message()`, `handle_auth()`, `handle_register()`, `handle_location()`, `log_event()`
- `src/server.c`: `server_broadcast()`
- `scripts/service.py`: `/auth`, `/register`, `/location`, `/log`

### Short Code Evidence
```c
/* src/protocol.c */
} else if (!client_info->authenticated) {
    send_json_response(client_info->sock,
      "{\"type\":\"error\",\"payload\":{\"message\":\"Authentication required\"}}");
}
```

```c
/* src/server.c */
if (type && strcmp(type->valuestring, MSG_TYPE_LOCATION) == 0) {
    if (server->clients[i].authenticated) {
        server_broadcast(server, buffer, sd);
    }
}
```

```python
# scripts/service.py
@app.route('/location', methods=['POST'])
def location():
    conn.execute("INSERT INTO locations (user_id, latitude, longitude) VALUES (?, ?, ?)", ...)
```

### Why This Satisfies Outcome 2
- Real concurrent socket handling is active while auth, storage, logs, and broadcast are all functioning.
- DB rows and logs provide verifiable traceability.

### Evidence to Show
- `docs/evidence/q2_db_snapshot.txt`
  - `users=32`, `locations=1978`, `logs=1764`

---

## Outcome 3: Socket Options Configuration + Performance Evaluation

### What Is Implemented
- Profile-driven socket options via `FISAC_SOCKOPTS_PROFILE` (`baseline` vs `tuned`).
- Automated profile runs with reproducible JSON outputs.
- Evaluated options:
  - `SO_REUSEADDR`
  - `SO_KEEPALIVE`
  - `SO_RCVBUF`
  - `TCP_NODELAY`

### Code Paths
- `src/socket.c`: `set_socket_options()`
- `run_all.bat`: profile propagation to backend + server
- `scripts/profile_eval.py`: automated baseline/tuned evaluation

### Measured Results (from `docs/evidence/q3_q4_summary.json`)
| Metric | Baseline | Tuned | Delta |
|---|---:|---:|---:|
| Throughput (msg/s) | 10.73 | 13.84 | +29.0% |
| p95 latency (ms) | 1.16 | 1.07 | -8.0% |
| Mean latency (ms) | 0.324 | 0.244 | Improved |
| Errors | 2 | 2 | Stable |

### Why This Satisfies Outcome 3
- Socket options are not only configured but quantitatively evaluated under scripted load.
- Claims are backed by machine-generated evidence JSON files.

### Evidence to Show
- `docs/evidence/load_baseline.json`
- `docs/evidence/load_tuned.json`
- `docs/evidence/q3_q4_summary.json`

---

## Outcome 4: Robustness Under Failure and Abnormal Traffic

### What Is Implemented
- Chaos scenarios: flood, unauthorized access, invalid credentials, rapid reconnect, malformed payload, idle delay.
- Client cleanup on disconnect in server loop.
- TCP state checks (`TIME_WAIT`, `CLOSE_WAIT`) in summary evidence.

### Code Paths
- `scripts/chaos_test.py`
- `src/server.c` (disconnect/cleanup path)
- `src/network.c` (`robust_send()`, `robust_recv()`)

### Measured Robustness Results
- `flood_sent`: 200
- `unauthorized_attempts`: 2
- `rapid_connect_disconnect`: 45
- `malformed_sent`: 0 (in saved summary)
- `errors`: 13
- `time_wait_8080`: 1
- `close_wait_8080`: 0

### Why This Satisfies Outcome 4
- Robustness is tested with adversarial scenarios, not just normal operation.
- TCP state evidence supports healthy cleanup behavior (`CLOSE_WAIT` not accumulating).

### Evidence to Show
- `docs/evidence/chaos_tuned.json`
- `docs/evidence/q3_q4_summary.json`
- `docs/evidence/q4_netstat_8080.txt`

---

## Extra Features Implemented Beyond Minimum Requirement

### Frontend and Observability
- Q1-Q4 alignment board.
- Workflow timeline (`connected`, `auth sent`, `auth accepted`, `subscribed`, `streaming`).
- Live highlights:
  - session distance (haversine)
  - updates/min
  - peak peers
  - session events
- Robustness counters:
  - reconnects
  - auth failures
  - malformed sent
  - geolocation fallbacks
- Backend evidence panel with periodic `/stats` polling.
- Protocol inspector (`Last TX`, `Last RX` JSON).
- Self/peer map trails.
- Exportable session snapshot JSON.

### Operational/Test Enhancements
- `run_all.bat` orchestration with profile switching and auto-launch.
- Baseline/tuned benchmark automation (`scripts/profile_eval.py`).
- Chaos harness (`scripts/chaos_test.py`).
- Concurrent load harness (`scripts/load_test.py`).
- Evidence artifacts generated under `docs/evidence/`.

### Why Extras Matter
- They make the project measurable, explainable, and demo-defensible, not just functional.

---

## Frontend Demo Scenarios (What Happens End-to-End)

| Scenario | Trigger | Client Behavior | Server/Backend Behavior | What Teacher Sees |
|---|---|---|---|---|
| Start Demo Route | `toggleDemoRoute()` | Stops geolocation watch, emits synthetic orbit coords every 1200 ms | Normal auth + ingest + broadcast + DB insert path | Smooth marker loop, growing trail, rising TX/RX |
| Run Burst x25 | `runBurstTest()` | Sends 25 location frames quickly | Parse/auth/store/broadcast spike | TX jumps, live logs spike, peer updates burst |
| Send Malformed Payload | `sendMalformedPayload()` | Sends intentionally broken JSON frame | Parse fails and drops message, server survives | Malformed counter increments; connection remains alive |
| Run Reconnect Drill | `runReconnectDrill()` | Closes and reopens socket, re-authenticates | Old socket cleanup + new handshake/auth sequence | Reconnect metric increments; workflow timeline replays |
| Clear Trails | `clearTrails()` | Clears local trail arrays and distance | No server impact | Visual reset on map |
| Export Snapshot | `exportSessionSnapshot()` | Builds and downloads session JSON artifact | No write path triggered | Downloaded reproducibility evidence file |
| Geolocation Fallback | `startSharing()` error branch | Uses simulated coordinates if geolocation fails | Normal location path still used | Demo continues even without GPS permission |

---

## Quick Code Map for Viva Navigation
- `src/server.c`: `server_init`, `server_run`, `server_broadcast`, `server_shutdown`
- `src/websocket.c`: handshake and frame encode/decode
- `src/network.c`: robust partial send/recv handling
- `src/socket.c`: profile-based socket options
- `src/protocol.c`: auth gate, dispatch, location ingest trigger, log forwarding
- `scripts/service.py`: backend API + SQLite persistence + `/stats`
- `frontend/app.js`: client state machine, metrics, demo controls
- `run_all.bat`: compile/start orchestration and profile selection

---

## Team Presentation Split
- Member 1: Outcome 1 (workflow + handshake + concurrency model)
- Member 2: Outcome 2 (auth + persistence + logs + broadcast)
- Member 3: Outcomes 3 and 4 (socket tuning + robustness evaluation)

## Evidence Commands for Rerun
```bash
python scripts/q1_workflow_capture.py
run_all.bat --no-pause tuned
python scripts/profile_eval.py
```
