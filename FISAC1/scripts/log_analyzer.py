# log_analyzer.py - Analyze system logs from the SQLite database

import sqlite3
import os
from datetime import datetime

DB_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "fisac.db")

def analyze_logs():
    if not os.path.exists(DB_PATH):
        print(f"[ERROR] Database not found: {DB_PATH}")
        print("Run the server first to create the database.")
        return

    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    print(f"[OK] Connected to database: {DB_PATH}")

    cursor = conn.cursor()

    # --- Summary Statistics ---
    print("\n" + "=" * 60)
    print("  LOG ANALYSIS REPORT")
    print("=" * 60)

    # Total log entries
    cursor.execute("SELECT COUNT(*) as total FROM logs")
    total = cursor.fetchone()['total']
    print(f"\nTotal log entries: {total}")

    # Logs by level
    print("\n--- Log Level Distribution ---")
    cursor.execute("SELECT level, COUNT(*) as count FROM logs GROUP BY level ORDER BY count DESC")
    for row in cursor.fetchall():
        bar = "█" * min(row['count'], 50)
        print(f"  {row['level']:8s} | {row['count']:5d} | {bar}")

    # --- Connection Analysis ---
    print("\n--- Connection Events ---")
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%New connection%'")
    connections = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%disconnected%'")
    disconnections = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%handshake%successful%'")
    handshakes = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%handshake%failed%'")
    failed_handshakes = cursor.fetchone()['count']

    print(f"  Total connections:     {connections}")
    print(f"  Total disconnections:  {disconnections}")
    print(f"  Successful handshakes: {handshakes}")
    print(f"  Failed handshakes:     {failed_handshakes}")

    # --- Authentication Analysis ---
    print("\n--- Authentication Events ---")
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%User authenticated%'")
    auth_ok = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%Failed auth attempt%'")
    auth_fail = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(*) as count FROM logs WHERE message LIKE '%Unauthorized message%'")
    unauthorized = cursor.fetchone()['count']

    print(f"  Successful logins:    {auth_ok}")
    print(f"  Failed login attempts: {auth_fail}")
    print(f"  Unauthorized access:  {unauthorized}")

    if auth_fail > 5:
        print(f"\n  [ALERT] {auth_fail} failed login attempts detected!")
        print("  Root Cause: Possible brute-force attack or incorrect credentials.")
        print("  Fix: Implement rate limiting and account lockout.")

    # --- Disconnect Pattern Analysis ---
    print("\n--- Disconnect Pattern Analysis ---")
    cursor.execute("""
        SELECT timestamp, message FROM logs
        WHERE message LIKE '%disconnected%'
        ORDER BY timestamp ASC
    """)
    disconnects = cursor.fetchall()

    if len(disconnects) >= 2:
        parse_fmt = "%Y-%m-%d %H:%M:%S"
        burst_count = 0
        max_burst = 0
        prev_time = None

        for row in disconnects:
            ts_str = row['timestamp'][:19]  # Trim microseconds
            try:
                ts = datetime.strptime(ts_str, parse_fmt)
                if prev_time and (ts - prev_time).total_seconds() < 1.0:
                    burst_count += 1
                    max_burst = max(max_burst, burst_count)
                else:
                    burst_count = 0
                prev_time = ts
            except ValueError:
                continue

        if max_burst > 5:
            print(f"  [ALERT] Disconnect burst detected: {max_burst + 1} disconnections in <1 second!")
            print("  Root Cause: Network issue, server crash, or coordinated client disconnect.")
            print("  Fix: Check server logs, verify network stability, add reconnect backoff.")
        else:
            print(f"  No abnormal disconnect bursts detected (max burst: {max_burst + 1}).")
    else:
        print("  Not enough disconnect data to analyze patterns.")

    # --- Location Data Statistics ---
    print("\n--- Location Data ---")
    cursor.execute("SELECT COUNT(*) as count FROM locations")
    loc_count = cursor.fetchone()['count']
    cursor.execute("SELECT COUNT(DISTINCT user_id) as count FROM locations")
    loc_users = cursor.fetchone()['count']
    print(f"  Total location updates: {loc_count}")
    print(f"  Unique users tracked:   {loc_users}")

    if loc_count > 0:
        cursor.execute("SELECT MIN(timestamp) as first, MAX(timestamp) as last FROM locations")
        row = cursor.fetchone()
        print(f"  First update: {row['first']}")
        print(f"  Last update:  {row['last']}")

    # --- User Statistics ---
    print("\n--- Registered Users ---")
    cursor.execute("SELECT id, username, created_at FROM users ORDER BY id")
    for row in cursor.fetchall():
        print(f"  ID={row['id']:3d}  {row['username']:20s}  (created: {row['created_at']})")

    # --- TIME_WAIT Analysis Note ---
    print("\n--- TCP State Analysis (TIME_WAIT / CLOSE_WAIT) ---")
    print("  TIME_WAIT and CLOSE_WAIT analysis requires OS-level tools.")
    print("  Run this command on the server machine during a load test:")
    print("    netstat -an | findstr :8080")
    print("  A large number of TIME_WAIT entries indicates rapid connection cycling.")
    print("  Fix: SO_REUSEADDR is enabled. For OS-level tuning, adjust TcpTimedWaitDelay")
    print("  in the Windows registry.")

    # --- Recent Errors ---
    print("\n--- Recent Errors (last 10) ---")
    cursor.execute("SELECT timestamp, level, message FROM logs WHERE level IN ('ERROR', 'WARN') ORDER BY timestamp DESC LIMIT 10")
    errors = cursor.fetchall()
    if errors:
        for row in errors:
            print(f"  [{row['level']}] {row['timestamp']} - {row['message']}")
    else:
        print("  No errors found. System is healthy.")

    print("\n" + "=" * 60)
    print("  END OF REPORT")
    print("=" * 60)

    conn.close()


if __name__ == "__main__":
    analyze_logs()
