#!/usr/bin/env python3
"""Exercise settings save/load in an isolated HOME with a real 64-bit executable."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="bordify-linux-test-") as home:
    env = os.environ.copy()
    env["HOME"] = home
    run = subprocess.run([sys.argv[1]], env=env, capture_output=True, text=True)
    if run.returncode:
        raise SystemExit(f"settings persistence failed ({run.returncode}): {run.stderr}")
    stored = Path(home) / ".config" / "bordify" / "settings.cfg"
    assert stored.is_file(), "settings file wasn't created"
    content = stored.read_text()
    assert "THEME=DARK" in content, content
    assert "WALLPAPER=DOTS" in content, content
    assert "ACCENT=GREEN" in content, content
    print("Bordify Linux 64-bit settings roundtrip: PASS")
