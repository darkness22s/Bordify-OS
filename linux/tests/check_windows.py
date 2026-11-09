#!/usr/bin/env python3
"""Verify that actual keyboard dispatch opens Bordify windows and mouse clicks save settings."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image, ImageChops

exe = str(Path(sys.argv[1]).resolve())
base = Image.open(sys.argv[2]).convert("RGB")
results = {}
for name in ("files", "web", "settings"):
    with tempfile.TemporaryDirectory(prefix="bordify-ui-") as home:
        outfile = Path(home) / "capture.bmp"
        env = dict(os.environ, HOME=home, SDL_VIDEODRIVER="dummy",
                   SDL_AUDIODRIVER="dummy", BORDIFY_SMOKE_FRAMES="11",
                   BORDIFY_SMOKE_BMP=str(outfile), BORDIFY_SMOKE_OPEN=name)
        if name == "settings":
            env["BORDIFY_SMOKE_INTERACT"] = "1"
        run = subprocess.run([exe], env=env, capture_output=True, text=True, timeout=30)
        assert run.returncode == 0, (name, run.stderr)
        current = Image.open(outfile).convert("RGB")
        assert current.size == base.size, (name, current.size)
        delta = ImageChops.difference(base, current)
        changed = sum(1 for p in delta.getdata() if p != (0, 0, 0))
        assert changed > 1000, (name, "Window failed to open", changed)
        results[name] = changed
        if name == "settings":
            cfg = Path(home) / ".config/bordify/settings.cfg"
            assert cfg.exists(), ("Settings buttons did not save", run.stderr)
            text = cfg.read_text()
            for expected in ("THEME=DARK", "WALLPAPER=GRID", "ACCENT=RED"):
                assert expected in text, (expected, text)
print("GUI shortcut and settings-click tests: PASS", results)
