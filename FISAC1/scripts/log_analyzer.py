# log_analyzer.py

import psycopg2
import pandas as pd
from datetime import timedelta

# --- Database Configuration ---
DB_NAME = "fisac"
DB_USER = "postgres"
DB_PASSWORD = "password"
DB_HOST = "localhost"
DB_PORT = "5432"

def analyze_logs():
    try:
        conn = psycopg2.connect(dbname=DB_NAME, user=DB_USER, password=DB_PASSWORD, host=DB_HOST, port=DB_PORT)
        print("Database connection successful.")
    except psycopg2.OperationalError as e:
        print(f"Could not connect to database: {e}")
        return

    try:
        # Fetch logs into a pandas DataFrame
        df = pd.read_sql("SELECT * FROM logs ORDER BY timestamp ASC", conn)
        print(f"Loaded {len(df)} log entries.")

        # --- Analysis ---

        # 1. Abnormal Disconnect Patterns
        disconnects = df[df['message'].str.contains("Host disconnected")]
        if not disconnects.empty:
            disconnects['timestamp'] = pd.to_datetime(disconnects['timestamp'])
            disconnects.set_index('timestamp', inplace=True)
            
            # Detect bursts of disconnections
            disconnect_bursts = disconnects.resample('1S').size()
            if disconnect_bursts.max() > 5:
                print(f"\n[ALERT] Abnormal disconnect pattern detected: {disconnect_bursts.max()} disconnections in 1 second.")
                print("Root Cause: This could be due to a network issue, a server crash, or a coordinated client-side problem.")
                print("Fixes: Investigate server logs around the time of the spike. Check for errors or high resource usage. Ensure clients have robust reconnect logic.")

        # 2. Failed DB Writes
        failed_writes = df[df['message'].str.contains("Failed to add location") | df['message'].str.contains("Failed to add log")]
        if not failed_writes.empty:
            print(f"\n[ALERT] {len(failed_writes)} failed database writes detected.")
            print("Root Cause: This could be due to database connection issues, constraint violations, or malformed data.")
            print("Fixes: Check the database logs for more specific errors. Ensure the Python service is correctly handling data and database connections.")

        # 3. Latency Spikes (Simulated)
        # In a real scenario, you would log latency from your load tester or the server itself.
        # Here, we'll just look for "high latency" in the log messages as a simulation.
        latency_spikes = df[df['message'].str.contains("high latency")]
        if not latency_spikes.empty:
            print(f"\n[ALERT] {len(latency_spikes)} latency spikes detected.")
            print("Root Cause: Latency spikes can be caused by network congestion, server overload, or slow database queries.")
            print("Fixes: Optimize database queries. Profile the server code to identify bottlenecks. Consider scaling up server resources.")
            
        # 4. TIME_WAIT buildup (Cannot be detected from these logs)
        print("\n[INFO] TIME_WAIT buildup analysis requires OS-level network statistics (e.g., from `netstat`). It cannot be determined from application logs alone.")
        print("To check for TIME_WAIT buildup, run a command like `netstat -an | grep TIME_WAIT | wc -l` on the server machine during the load test.")
        print("A large number of connections in TIME_WAIT can indicate that the server is not reusing sockets efficiently.")
        print("Fixes: Ensure SO_REUSEADDR is set on the server socket. Tune TCP parameters at the OS level if necessary.")


    except Exception as e:
        print(f"An error occurred during analysis: {e}")
    finally:
        conn.close()


if __name__ == "__main__":
    analyze_logs()
