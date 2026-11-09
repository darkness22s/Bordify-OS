#!/usr/bin/env python3
"""Select an application in the graphical session, safely passing its full path."""
import shutil
import subprocess
import sys

def main():
    if not shutil.which("zenity"):
        print("Install zenity for the graphical file picker", file=sys.stderr)
        return 127
    bundle = "--bundle" in sys.argv[1:]
    command = ["zenity", "--file-selection"]
    if bundle:
        command.append("--directory")
    command.append("--title=" + ("Open a macOS .app bundle" if bundle else "Open an app in Bordify OS"))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        return 0 if result.returncode == 1 else result.returncode
    chosen = result.stdout.strip()
    if chosen:
        return subprocess.run(["bordify-open", chosen], check=False).returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
