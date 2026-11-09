#!/usr/bin/env python3
"""Select an application in the graphical session, safely passing its full path."""
import shutil
import subprocess
import sys

def main():
    if not shutil.which("zenity"):
        print("Install zenity for the graphical file picker", file=sys.stderr)
        return 127
    result = subprocess.run(["zenity", "--file-selection",
                             "--title=Open an app in Bordify OS"], capture_output=True, text=True)
    if result.returncode:
        return 0 if result.returncode == 1 else result.returncode
    chosen = result.stdout.strip()
    if chosen:
        return subprocess.run(["bordify-open", chosen], check=False).returncode
    return 0

if __name__ == "__main__":
    sys.exit(main())
