#!/usr/bin/env python3
"""Bounded test of genuine Darling Darwin process execution.

Never allow a stuck Darling daemon to block all CI jobs. Capture output in
a regular file (not a pipe held open by daemon grandchildren).
"""
import os
from pathlib import Path
import signal
import subprocess
import sys

out = Path("build/darling")
out.mkdir(parents=True, exist_ok=True)

def run(name, cmd, seconds=45):
    log = out / (name + ".log")
    with log.open("wb") as handle:
        proc = subprocess.Popen(cmd, stdout=handle, stderr=subprocess.STDOUT,
                                stdin=subprocess.DEVNULL, start_new_session=True)
        try:
            returncode = proc.wait(timeout=seconds)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            print(f"{name}: timed out after {seconds}s", file=sys.stderr)
            print(log.read_text(errors="replace")[-12000:], file=sys.stderr)
            raise SystemExit(124)
    data = log.read_text(errors="replace")
    print(f"=== {name} (exit={returncode}) ===\n{data[-12000:]}", flush=True)
    if returncode != 0:
        raise SystemExit(returncode)
    return data

uname = run("darling-uname", ["darling", "shell", "uname", "-s"])
if "Darwin" not in uname:
    raise SystemExit("Darling did not actually report Darwin")
run("darling-true", ["darling", "shell", "/usr/bin/true"])
print("Real Darling /usr/bin/true and Darwin uname: PASS")
