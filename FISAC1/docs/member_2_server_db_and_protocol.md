Member 2: Question 2 - Bullet Answer (Server + Auth + Storage + Broadcast)

Write in bullets:

- Concurrent server behavior
	- C server runs select()-based multi-client loop.
	- Each active socket monitored for readable events.
	- Client slots maintain auth state and user identity.

- Authentication pipeline
	- Client sends auth/register JSON over WebSocket.
	- C server forwards credentials to Python endpoints (/auth, /register).
	- Strict JSON auth_response returned to client.
	- Successful auth enables location publish and subscription actions.

- Continuous location ingestion
	- Authenticated clients send latitude/longitude updates.
	- C server forwards updates to backend /location endpoint.
	- Backend persists updates with timestamps in SQLite.

- Broadcast path
	- Server rebroadcasts location updates to other authorized connected clients.
	- Unauthorized messages rejected with error response.

- Partial transmission and latency handling
	- robust_send and robust_recv used for partial I/O handling.
	- WebSocket parser supports masking and extended payload lengths.
	- Non-data control frames handled without unnecessary disconnect.

- Syslog and DB consistency
	- Event logs sent via /log endpoint and stored in logs table.
	- Core tables used: users, locations, logs.
	- Correlation possible between auth events, location inserts, and broadcasts.

- Screenshot evidence to attach
	- Browser login + live location updates
	- .backend.log lines for /auth, /location, /log
	- SQLite table snapshots: users, locations, logs
