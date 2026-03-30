import json
import os
import subprocess
import time
import argparse
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "docs", "evidence")
PYTHON = sys.executable


def run_cmd(cmd: str) -> int:
    try:
        result = subprocess.run(cmd, shell=True, cwd=ROOT, check=False)
        return result.returncode
    except KeyboardInterrupt:
        print("\n[INFO] Interrupted by user (Ctrl+C).")
        return 130


def kill_services() -> None:
    run_cmd("taskkill /f /im server.exe >nul 2>&1")
    run_cmd("for /f \"tokens=5\" %T in ('netstat -ano ^| findstr LISTENING ^| findstr :5000') do @if not \"%T\"==\"0\" taskkill /f /pid %T >nul 2>&1")
    run_cmd("for /f \"tokens=5\" %T in ('netstat -ano ^| findstr LISTENING ^| findstr :8080') do @if not \"%T\"==\"0\" taskkill /f /pid %T >nul 2>&1")


def run_profile(profile: str, clients: int, duration: int, interval: float) -> dict:
    os.makedirs(OUT_DIR, exist_ok=True)
    result_json = os.path.join(OUT_DIR, f"load_{profile}.json")

    kill_services()
    rc = run_cmd(f"run_all.bat --no-pause {profile}")
    if rc != 0:
        if rc == 130:
            return {"profile": profile, "cancelled": True}
        return {"profile": profile, "error": f"run_all failed rc={rc}"}

    time.sleep(3)
    rc = run_cmd(
        f"\"{PYTHON}\" scripts\\load_test.py "
        f"--clients {clients} --duration {duration} --interval {interval} "
        f"--output-json \"{result_json}\""
    )
    if rc != 0:
        if rc == 130:
            return {"profile": profile, "cancelled": True}
        return {"profile": profile, "error": f"load_test failed rc={rc}"}

    try:
        with open(result_json, "r", encoding="utf-8") as f:
            data = json.load(f)
        data["profile"] = profile
        return data
    except Exception as exc:
        return {"profile": profile, "error": repr(exc)}


def netstat_state_counts() -> dict:
    out = subprocess.check_output("netstat -ano", shell=True, cwd=ROOT, text=True, errors="ignore")
    tw = sum(1 for line in out.splitlines() if ":8080" in line and "TIME_WAIT" in line)
    cw = sum(1 for line in out.splitlines() if ":8080" in line and "CLOSE_WAIT" in line)
    return {"time_wait_8080": tw, "close_wait_8080": cw}


def main() -> None:
    parser = argparse.ArgumentParser(description="Evaluate baseline vs tuned socket profiles")
    parser.add_argument("--clients", type=int, default=30)
    parser.add_argument("--duration", type=int, default=20)
    parser.add_argument("--interval", type=float, default=0.2)
    parser.add_argument("--skip-baseline", action="store_true")
    parser.add_argument("--skip-tuned", action="store_true")
    parser.add_argument("--skip-chaos", action="store_true")
    args = parser.parse_args()

    os.makedirs(OUT_DIR, exist_ok=True)

    baseline = None
    tuned = None

    try:
        if not args.skip_baseline:
            baseline = run_profile("baseline", args.clients, args.duration, args.interval)
            if isinstance(baseline, dict) and baseline.get("cancelled"):
                raise KeyboardInterrupt

        if not args.skip_tuned:
            tuned = run_profile("tuned", args.clients, args.duration, args.interval)
            if isinstance(tuned, dict) and tuned.get("cancelled"):
                raise KeyboardInterrupt

        # Robustness evidence after tuned run
        chaos_json = os.path.join(OUT_DIR, "chaos_tuned.json")
        if not args.skip_chaos:
            rc = run_cmd(f"\"{PYTHON}\" scripts\\chaos_test.py --output-json \"{chaos_json}\"")
            if rc == 130:
                raise KeyboardInterrupt

    except KeyboardInterrupt:
        print("[INFO] profile_eval interrupted. Writing partial summary...")

    state_counts = netstat_state_counts()

    summary = {
        "baseline": baseline,
        "tuned": tuned,
        "tcp_states": state_counts,
        "notes": "Use load_*.json, chaos_tuned.json and terminal screenshots as Q3/Q4 evidence.",
    }

    out = os.path.join(OUT_DIR, "q3_q4_summary.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print(f"[OK] Wrote profile summary: {out}")
    kill_services()


if __name__ == "__main__":
    main()
