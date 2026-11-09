#!/usr/bin/env python3
"""Check a real headless SDL frame, rather than only a successful exit code."""
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
assert im.size == (1024, 768), im.size
assert im.getpixel((600, 400)) == (255, 255, 255), "Wallpaper changed"
# The three original shortcuts should have nonwhite pixels in their icon regions.
for area in [(50, 64, 106, 120), (50, 146, 106, 202), (50, 228, 106, 284)]:
    region = im.crop(area)
    assert any(pixel != (255, 255, 255) for pixel in region.getdata()), area
# The rounded bottom dock must still exist.
assert im.getpixel((512, 740)) != (0, 0, 0), "Dock missing"
print("Bordify native Linux SDL desktop screenshot: PASS")
