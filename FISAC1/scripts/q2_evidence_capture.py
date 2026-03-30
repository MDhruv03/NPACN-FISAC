import os
import sqlite3
import subprocess
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVIDENCE_DIR = os.path.join(ROOT, "docs", "evidence")
DB_PATH = os.path.join(ROOT, "fisac.db")


def tail_lines(path: str, n: int = 120) -> str:
    if not os.path.exists(path):
        return f"[MISSING] {path}\n"
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    return "".join(lines[-n:])


def write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def db_snapshot() -> str:
    if not os.path.exists(DB_PATH):
        return f"[MISSING] Database file not found: {DB_PATH}\n"

    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    out = []
    out.append("Q2 DB Snapshot")
    out.append(f"Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    out.append("")

    for table in ["users", "locations", "logs"]:
        cur.execute(f"SELECT COUNT(*) AS c FROM {table}")
        c = cur.fetchone()["c"]
        out.append(f"Table {table}: count = {c}")

    out.append("\nRecent users (max 10):")
    cur.execute("SELECT id, username, created_at FROM users ORDER BY id DESC LIMIT 10")
    for r in cur.fetchall():
        out.append(f"id={r['id']} username={r['username']} created_at={r['created_at']}")

    out.append("\nRecent locations (max 10):")
    cur.execute("SELECT user_id, latitude, longitude, timestamp FROM locations ORDER BY id DESC LIMIT 10")
    for r in cur.fetchall():
        out.append(
            f"user_id={r['user_id']} lat={r['latitude']:.6f} lon={r['longitude']:.6f} ts={r['timestamp']}"
        )

    out.append("\nRecent logs (max 20):")
    cur.execute("SELECT level, message, timestamp FROM logs ORDER BY id DESC LIMIT 20")
    for r in cur.fetchall():
        out.append(f"[{r['level']}] {r['timestamp']} {r['message']}")

    conn.close()
    return "\n".join(out) + "\n"


def netstat_8080() -> str:
    try:
        out = subprocess.check_output("netstat -ano | findstr :8080", shell=True, cwd=ROOT, text=True, errors="replace")
        return out.strip() + "\n"
    except subprocess.CalledProcessError:
        return "(no entries for :8080)\n"


def main() -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)

    backend_log = os.path.join(ROOT, ".backend.log")
    server_log = os.path.join(ROOT, ".server.log")

    write_text(os.path.join(EVIDENCE_DIR, "q2_backend_log_tail.txt"), tail_lines(backend_log, 150))
    write_text(os.path.join(EVIDENCE_DIR, "q2_server_log_tail.txt"), tail_lines(server_log, 150))
    write_text(os.path.join(EVIDENCE_DIR, "q2_db_snapshot.txt"), db_snapshot())
    write_text(os.path.join(EVIDENCE_DIR, "q4_netstat_8080.txt"), netstat_8080())

    print("[OK] Generated Q2/Q4 evidence text files in docs/evidence")


if __name__ == "__main__":
    main()
