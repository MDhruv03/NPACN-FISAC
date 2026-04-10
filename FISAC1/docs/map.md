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

---

## Viva Drill-Down Mode (When Teacher Keeps Adding Questions)

Use this answer ladder for any point:
1. Layer A (10 sec): one-line statement.
2. Layer B (30 sec): architecture + implementation summary.
3. Layer C (90 sec): exact code path + runtime behavior.
4. Layer D (2-3 min): trade-offs, failure modes, and evidence-backed defense.

If teacher asks deeper, keep extending in this order:
1. Protocol level
2. Code level
3. System behavior under stress
4. Limitation and future improvement

---

## Outcome 1 Deep Defense Tree

### Layer A (10 sec)
We establish TCP, upgrade to WebSocket with RFC6455 handshake, exchange full-duplex JSON frames, and handle disconnects in a concurrent `select()` loop.

### Layer B (30 sec)
Client connects on TCP `:8080`, server performs HTTP upgrade using `Sec-WebSocket-Key`, then both sides use WebSocket text frames for auth and location traffic. The server remains single-threaded and multiplexes clients with `select()`, which is sufficient for this I/O-heavy workload.

### Layer C (90 sec)
Connection lifecycle:
1. Server accepts new TCP socket in `server_run()`.
2. `websocket_handshake()` reads upgrade request and computes accept token.
3. On successful `101 Switching Protocols`, client is added to `clients[]`.
4. `websocket_frame_recv()` decodes frame header, masking, payload length, and control opcodes.
5. `handle_message()` routes JSON message types.
6. On close/error, socket is closed and client slot is reset.

Handshake equation:
```text
Sec-WebSocket-Accept = Base64(SHA1(Sec-WebSocket-Key + GUID))
```

### Layer D (2-3 min with follow-ups)
Concurrency justification details:
- `select()` avoids mutex/race complexity of threads.
- For this assignment scale (tens of sockets), one loop is predictable and easy to validate.
- Drawback: a blocking operation in loop can delay others.
- Current mitigation: local backend + bounded HTTP timeouts.

Likely teacher follow-ups and ready answers:
1. Why not threads?
  - Fewer race bugs, lower complexity, enough for I/O-bound workload.
2. Why WebSocket not raw TCP?
  - Browser-native full-duplex and framing.
3. How do you handle control frames?
  - Ping gets pong, close leads to cleanup.
4. How do you prove handshake happened?
  - Show `docs/q1_workflow_capture.txt` with `101 Switching Protocols`.

Hard follow-up (very likely):
"Accepted sockets are inherited non-blocking on Windows, so why set them blocking?"
- Answer: we intentionally force accepted client sockets to blocking so frame assembly in `recv_exact()` stays deterministic and simpler for project scope.

Hard follow-up 2:
"What if HTTP upgrade headers arrive in fragments?"
- Answer: handshake function loops until `\r\n\r\n`, so partial header arrival is handled.

---

## Outcome 2 Deep Defense Tree

### Layer A (10 sec)
We implemented an authenticated concurrent server that ingests live location frames, persists timestamped data, logs events, and rebroadcasts to authorized peers.

### Layer B (30 sec)
Client sends `auth` or `register` over WebSocket. C server forwards to Flask (`/auth`, `/register`), receives result, marks client authenticated, then accepts `location` frames. For each location, server triggers persistence via `/location` and broadcasts to other authenticated sockets.

### Layer C (90 sec)
Auth gate behavior in dispatcher:
1. Parse incoming JSON in `handle_message()`.
2. If `type` is `auth` or `register`, allow pre-auth path.
3. Else if not authenticated, return error frame (`Authentication required`).
4. Only authenticated clients can push location stream and receive broadcast.

Persistence/logging behavior:
1. `handle_location()` builds JSON and posts to backend `/location`.
2. Backend inserts row into `locations` with timestamp.
3. `log_event()` posts server lifecycle/security events to `/log`.

### Layer D (2-3 min with follow-ups)
Likely teacher follow-ups and ready answers:
1. How do you prevent unauthorized writes?
  - Protocol gate rejects non-authenticated message types.
2. How do you prove DB writes happened?
  - Show `docs/evidence/q2_db_snapshot.txt` counts and recent rows.
3. How do you prove broadcast happened?
  - Show multi-client map update + RX counter + protocol inspector.
4. Are logs and DB traceable together?
  - Yes, auth/connect/disconnect and location timelines are visible in logs and table timestamps.

Hard follow-up:
"Can client spoof `userId` in payload?"
- Answer: DB write uses authenticated `user_id` from server session state, not payload string. UI label can still show client payload name, so production hardening would enforce server-side canonical identity in outbound payload too.

Hard follow-up 2:
"What is your biggest bottleneck in Outcome 2?"
- Answer: synchronous C->Flask HTTP call in same event thread can become head-of-line blocking if backend slows down.

---

## Outcome 3 Deep Defense Tree

### Layer A (10 sec)
We configured all required socket options and proved impact by comparing baseline and tuned profiles with measured load-test evidence.

### Layer B (30 sec)
`set_socket_options()` applies profile-based settings. `run_all.bat` sets profile env var, and `profile_eval.py` runs baseline and tuned test rounds, then exports JSON metrics.

### Layer C (90 sec)
Option-specific reasoning:
1. `SO_REUSEADDR`: fast restart despite `TIME_WAIT`.
2. `SO_KEEPALIVE`: dead peer detection.
3. `SO_RCVBUF`: burst absorption under high receive rate.
4. `TCP_NODELAY`: lower delay for small frequent frames.

Measured output:
- Throughput: `10.73 -> 13.84 msg/s`.
- p95 latency: `1.16 -> 1.07 ms`.
- Mean latency: `0.324 -> 0.244 ms`.
- Errors unchanged: `2 -> 2`.

### Layer D (2-3 min with follow-ups)
Likely teacher follow-ups and ready answers:
1. Why is throughput gain larger than p95 gain?
  - Buffering and send behavior improved aggregate flow; p95 was already low baseline, so absolute room for p95 reduction was limited.
2. Why errors unchanged?
  - Options tuned performance, not functional correctness path.
3. Why not claim statistical significance?
  - Current script does deterministic two-profile capture; for formal significance we would run repeated trials and confidence intervals.

Hard follow-up:
"Could `TCP_NODELAY` increase packet overhead?"
- Answer: yes, but for low-size real-time location messages, latency priority is higher than packet coalescing benefit.

Hard follow-up 2:
"What exactly changes between baseline and tuned in your code?"
- Answer: `SO_REUSEADDR`, `SO_KEEPALIVE`, and `TCP_NODELAY` toggle 0/1 by profile, and `SO_RCVBUF` shifts from 8 KB baseline to 128 KB tuned.

---

## Outcome 4 Deep Defense Tree

### Layer A (10 sec)
We stress-tested failure and abuse scenarios and validated behavior using chaos metrics plus TCP state and DB/log checks.

### Layer B (30 sec)
`chaos_test.py` runs flood, unauthorized, invalid auth, reconnect cycles, malformed payloads, and idle delay. We collect counts in `chaos_tuned.json` and summarize with TCP state checks in `q3_q4_summary.json`.

### Layer C (90 sec)
Observed outputs:
- Flood sent: `200`
- Unauthorized attempts: `2`
- Rapid reconnect cycles: `45`
- Errors: `13`
- TCP state: `time_wait_8080=1`, `close_wait_8080=0`

Interpretation:
- System survives flood and reconnect churn.
- Unauthorized attempts are blocked.
- No `CLOSE_WAIT` accumulation indicates cleanup path is functioning.

### Layer D (2-3 min with follow-ups)
Likely teacher follow-ups and ready answers:
1. Difference between `TIME_WAIT` and `CLOSE_WAIT`?
  - `TIME_WAIT` expected on active close side; `CLOSE_WAIT` suggests app not closing after peer FIN.
2. Why is `close_wait_8080=0` important?
  - Indicates no persistent socket cleanup leak in observed run.
3. Why can malformed count differ between frontend demo and chaos summary?
  - Frontend counter and chaos script use different test routes and output files; summary reflects script-run dataset.

Hard follow-up:
"How do you prove DB consistency after stress?"
- Answer: compare log and location growth snapshots and inspect recent rows in `q2_db_snapshot.txt` after stress runs.

Hard follow-up 2:
"What remains vulnerable?"
- Answer: no server-side rate limiter and no TLS in local demo path.

---

## End-to-End Message Sequences You Can Speak Verbally

### Sequence 1: Login Success
1. Browser connects `ws://localhost:8080`.
2. WebSocket handshake succeeds (`101`).
3. Browser sends:
```json
{"type":"auth","payload":{"username":"user1","password":"pass1"}}
```
4. C server posts credentials to backend `/auth`.
5. Backend validates SQLite user row and returns success.
6. C server marks client authenticated and sends `auth_response`.

### Sequence 2: Location Broadcast
1. Authenticated browser sends:
```json
{"type":"location","payload":{"latitude":28.61,"longitude":77.20,"userId":"user1"}}
```
2. C server validates session is authenticated.
3. C server posts location to `/location` for persistence.
4. C server broadcasts original frame to other authenticated clients.
5. Other clients update marker and trail.

### Sequence 3: Unauthorized Location Attempt
1. Unauthenticated socket sends `location`.
2. Dispatcher rejects and returns:
```json
{"type":"error","payload":{"message":"Authentication required"}}
```
3. Log entry records unauthorized attempt.

---

## Demo Scenario Expansion (Teacher Drill Version)

### Start Demo Route: extended defense
What to say first:
- "This creates deterministic movement to validate ingest-broadcast-persist loop independent of GPS quality."

If asked deeper:
1. It stops geolocation watcher and uses timer-based orbit points.
2. Each point follows normal `location` protocol path.
3. We verify with TX/RX counters, trail growth, and backend location count.

### Run Burst x25: extended defense
What to say first:
- "This is a controlled micro-stress run for high-frequency frame handling."

If asked deeper:
1. Sends 25 frames with small coordinate offsets.
2. Exercises parser, auth gate, DB write path, and broadcast fanout.
3. Visual confirmation through fast metric and log increments.

### Send Malformed Payload: extended defense
What to say first:
- "This validates parser resilience; bad input should be dropped without taking down the server."

If asked deeper:
1. Invalid JSON sent directly over existing WebSocket.
2. `cJSON_Parse` fails and dispatcher returns without crash.
3. Session remains alive for next valid messages.

### Reconnect Drill: extended defense
What to say first:
- "This validates recovery after transient network loss."

If asked deeper:
1. Client closes socket then reconnects using last credentials.
2. Server clears old slot and accepts new handshake/auth.
3. Timeline and reconnect counter visibly update.

### Export Snapshot: extended defense
What to say first:
- "This generates reproducible evidence at the exact demo moment."

If asked deeper:
1. Captures session metrics, backend stats, peers, protocol traces.
2. Downloads JSON artifact for post-demo review.
3. Useful when viva asks to prove what was happening live.

---

## Cross-Outcome Linking Lines (Use When Teacher Connects Questions)

If teacher links Outcome 1 -> Outcome 2:
- "Handshake and `select()` establish transport and concurrency, then protocol/auth logic controls who can enter data and receive broadcast."

If teacher links Outcome 2 -> Outcome 3:
- "After functional correctness, socket tuning improves performance of the same ingest+broadcast path under load."

If teacher links Outcome 3 -> Outcome 4:
- "Performance tuning and robustness tests together show not only speed gains but stable behavior under abnormal traffic and reconnect churn."

If teacher asks final synthesis:
- "Outcome 1 proves communication correctness, Outcome 2 proves application correctness, Outcome 3 proves performance optimization, Outcome 4 proves failure resilience."

---

## Honest Limitations (Say Confidently if Challenged)

1. `ws://` localhost demo only, no TLS termination (`wss://`) in current build.
2. C server uses synchronous localhost HTTP bridge to backend.
3. No server-side rate limiter/quota per client.
4. Broadcast payload can carry client-provided display identity string.

Use this closing line:
- "These are known and documented constraints for the project scope, and they do not invalidate the required outcomes, which are fully demonstrated with code and evidence."

---

## Rapid-Fire Question Bank (Teacher Style)

1. What exact event transitions a socket from unauthenticated to authenticated?
2. Why does your `select()` loop use timeout instead of blocking forever?
3. What happens when `websocket_frame_recv()` sees opcode `0x8`?
4. How do you guarantee full message send under partial TCP writes?
5. How do you ensure location writes are timestamped?
6. How do you prove unauthorized requests are blocked?
7. Which option helps immediate restart after server crash?
8. Why is `SO_RCVBUF` useful in burst traffic?
9. Why can tuned throughput improve even if error count is unchanged?
10. What does `close_wait_8080=0` tell you?
11. What would you change first for production hardening?
12. Which artifact would you show first to prove Q3?

Short direct answers:
1. Successful `auth_response` path sets `client->authenticated=1`.
2. Allows periodic maintenance and avoids permanent blocking.
3. Returns disconnect path, server closes socket and clears slot.
4. `robust_send()` loops until all bytes are sent or fatal error.
5. SQLite schema uses default timestamp columns.
6. Dispatcher returns explicit error for non-authenticated message types.
7. `SO_REUSEADDR`.
8. Absorbs burst arrivals and reduces flow stalls.
9. Tuning affects transport efficiency, not semantic validation logic.
10. No observed socket cleanup leak in measured run.
11. Add TLS, async backend queue, and server-side rate limit.
12. `docs/evidence/q3_q4_summary.json` plus `load_baseline.json` and `load_tuned.json`.

