import json
import os
import socket
import subprocess
import sys
import time
from typing import Any, Dict, List

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EVIDENCE_DIR = os.path.join(ROOT, "docs", "evidence")
PYTHON = sys.executable
GCC = r"C:\msys64\ucrt64\bin\gcc.exe"


def ensure_dir() -> None:
    os.makedirs(EVIDENCE_DIR, exist_ok=True)


def load_json(path: str) -> Dict[str, Any]:
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def wait_for_port(port: int, timeout_s: int = 20) -> bool:
    end = time.time() + timeout_s
    while time.time() < end:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.5)
        try:
            s.connect(("127.0.0.1", port))
            s.close()
            return True
        except OSError:
            s.close()
            time.sleep(0.25)
    return False


def run_cmd(args: List[str], env: Dict[str, str] = None) -> int:
    proc = subprocess.run(args, cwd=ROOT, env=env, check=False)
    return proc.returncode


def ensure_server_binary() -> None:
    exe_path = os.path.join(ROOT, "server.exe")
    if os.path.exists(exe_path):
        return

    if not os.path.exists(GCC):
        raise RuntimeError(f"GCC not found at {GCC}; cannot build server.exe")

    obj_dir = os.path.join(ROOT, "obj")
    os.makedirs(obj_dir, exist_ok=True)

    sources = [
        "main",
        "server",
        "socket",
        "network",
        "websocket",
        "protocol",
        "http_client",
        "sha1",
        "base64",
        "cJSON",
    ]

    for name in sources:
        src = os.path.join(ROOT, "src", f"{name}.c")
        obj = os.path.join(ROOT, "obj", f"{name}.o")
        rc = run_cmd([GCC, "-Wall", "-Wextra", "-I./include", "-g", "-c", src, "-o", obj])
        if rc != 0:
            raise RuntimeError(f"compile failed for {src}, rc={rc}")

    objects = [os.path.join(ROOT, "obj", f"{name}.o") for name in sources]
    link_cmd = [GCC, "-o", exe_path, *objects, "-lws2_32"]
    rc = run_cmd(link_cmd)
    if rc != 0:
        raise RuntimeError(f"link failed for server.exe, rc={rc}")


def kill_server_processes() -> None:
    subprocess.run(["taskkill", "/f", "/im", "server.exe"], cwd=ROOT, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def kill_port_5000_listener() -> None:
    # Use cmd pipeline to identify a LISTENING PID on :5000 and kill it.
    cmd = "for /f \"tokens=5\" %T in ('netstat -ano ^| findstr LISTENING ^| findstr :5000') do @if not \"%T\"==\"0\" taskkill /f /pid %T"
    subprocess.run(["cmd", "/c", cmd], cwd=ROOT, check=False, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def start_backend() -> subprocess.Popen:
    log_path = os.path.join(ROOT, ".backend.log")
    log_file = open(log_path, "w", encoding="utf-8", errors="replace")
    proc = subprocess.Popen([PYTHON, "scripts/service.py"], cwd=ROOT, stdout=log_file, stderr=subprocess.STDOUT)
    if not wait_for_port(5000, timeout_s=20):
        proc.terminate()
        raise RuntimeError("Backend did not start on port 5000")
    return proc


def start_server(profile: str) -> subprocess.Popen:
    log_path = os.path.join(ROOT, ".server.log")
    log_file = open(log_path, "w", encoding="utf-8", errors="replace")
    env = os.environ.copy()
    env["SOCKET_PROFILE"] = profile
    proc = subprocess.Popen([os.path.join(ROOT, "server.exe")], cwd=ROOT, env=env, stdout=log_file, stderr=subprocess.STDOUT)
    if not wait_for_port(8080, timeout_s=20):
        proc.terminate()
        raise RuntimeError(f"Server did not start on port 8080 for profile={profile}")
    return proc


def terminate_process(proc: subprocess.Popen) -> None:
    if proc is None:
        return
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def run_load_test(profile: str, clients: int, duration: int, interval: float) -> Dict[str, Any]:
    out_path = os.path.join(EVIDENCE_DIR, f"load_{profile}.json")
    rc = run_cmd([
        PYTHON,
        "scripts/load_test.py",
        "--clients",
        str(clients),
        "--duration",
        str(duration),
        "--interval",
        str(interval),
        "--output-json",
        out_path,
    ])
    if rc != 0:
        raise RuntimeError(f"load_test failed for profile={profile}, rc={rc}")
    return load_json(out_path)


def run_chaos_test() -> Dict[str, Any]:
    out_path = os.path.join(EVIDENCE_DIR, "chaos_tuned.json")
    rc = run_cmd([
        PYTHON,
        "scripts/chaos_test.py",
        "--output-json",
        out_path,
    ])
    if rc != 0:
        raise RuntimeError(f"chaos_test failed rc={rc}")
    return load_json(out_path)


def netstat_state_counts() -> Dict[str, int]:
    out = subprocess.check_output(["cmd", "/c", "netstat -ano"], cwd=ROOT, text=True, errors="ignore")
    tw = sum(1 for line in out.splitlines() if ":8080" in line and "TIME_WAIT" in line)
    cw = sum(1 for line in out.splitlines() if ":8080" in line and "CLOSE_WAIT" in line)
    return {"time_wait_8080": tw, "close_wait_8080": cw}


def write_summary(summary: Dict[str, Any]) -> None:
    out_path = os.path.join(EVIDENCE_DIR, "q3_q4_summary.json")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)


def main() -> None:
    ensure_dir()
    ensure_server_binary()
    kill_server_processes()
    kill_port_5000_listener()

    backend = None
    server = None

    try:
        print("[1/5] Starting backend...")
        backend = start_backend()

        print("[2/5] Running baseline profile load test...")
        server = start_server("baseline")
        baseline = run_load_test("baseline", clients=20, duration=10, interval=0.2)
        terminate_process(server)
        server = None

        print("[3/5] Running tuned profile load test...")
        server = start_server("tuned")
        tuned = run_load_test("tuned", clients=20, duration=10, interval=0.2)

        print("[4/5] Running chaos robustness test on tuned server...")
        chaos = run_chaos_test()

        print("[5/5] Capturing TCP states and writing summary...")
        summary = {
            "baseline": baseline,
            "tuned": tuned,
            "chaos_tuned": chaos,
            "tcp_states": netstat_state_counts(),
            "notes": "Generated by scripts/generate_q4_evidence.py",
        }
        write_summary(summary)
        print("[OK] Q4 evidence generated in docs/evidence")

    finally:
        terminate_process(server)
        terminate_process(backend)
        kill_server_processes()


if __name__ == "__main__":
    main()
