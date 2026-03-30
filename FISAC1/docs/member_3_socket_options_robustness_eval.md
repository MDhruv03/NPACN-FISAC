Member 3: Questions 3 and 4 - Bullet Answer (Socket Options + Robustness)

Write in bullets:

- Socket options configured
	- SO_REUSEADDR: restart without long bind wait.
	- SO_KEEPALIVE: dead peer detection for stale sessions.
	- SO_RCVBUF: larger receive buffer for burst traffic.
	- TCP_NODELAY: reduced delay for small frequent frames.

- Experimental design
	- Profile A: baseline (options off, small receive buffer).
	- Profile B: tuned (options on, larger receive buffer).
	- Tooling: scripts/profile_eval.py + scripts/load_test.py.
	- Metrics captured: throughput, errors, latency min/max/mean, p50/p95/p99.

- Fill these figures from evidence JSON
	- Baseline throughput: 10.73 msg/s
	- Tuned throughput: 13.84 msg/s
	- Baseline p95 latency: 1.16 ms
	- Tuned p95 latency: 1.07 ms
	- Baseline errors: 2
	- Tuned errors: 2

- Robustness evaluation scenarios
	- Sudden disconnect and rapid reconnect cycles.
	- High-frequency location update bursts.
	- Malformed payload injection.
	- Unauthorized message/auth attempts.
	- Idle delay followed by resumed traffic.

- Robustness outcomes (fill from chaos output)
	- Flood messages processed: 200
	- Unauthorized attempts blocked: 2
	- Rapid connect/disconnect cycles: 45
	- Malformed messages handled (recorded): 0 in summary counter
	- Total errors observed: 13

- TCP state and consistency analysis
	- TIME_WAIT on :8080 observed under reconnect load (count: 1, expected transient state).
	- CLOSE_WAIT count monitored to detect cleanup regressions (count: 0).
	- DB and logs checked for consistency after stress runs.

- Screenshot evidence to attach
	- docs/evidence/load_baseline.json
	- docs/evidence/load_tuned.json
	- docs/evidence/chaos_tuned.json
	- docs/evidence/q3_q4_summary.json
	- netstat output for :8080 state counts
