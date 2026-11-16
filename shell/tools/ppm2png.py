#!/usr/bin/env python3
"""Convert headless screenshots (PPM) to 2x PNGs and build a contact sheet."""
import glob
import os
import sys

from PIL import Image

out = sys.argv[1]
files = sorted(glob.glob(os.path.join(out, "*.ppm")))
W, H, PAD, COLS = 640, 340, 16, 3
rows = (len(files) + COLS - 1) // COLS
sheet = Image.new("RGB", (COLS * (W + PAD) + PAD, rows * (H + PAD) + PAD), (40, 40, 40))
for i, f in enumerate(files):
    big = Image.open(f).convert("RGB").resize((W, H), Image.NEAREST)
    big.save(f[:-4] + ".png")
    os.remove(f)
    sheet.paste(big, (PAD + (i % COLS) * (W + PAD), PAD + (i // COLS) * (H + PAD)))
sheet.save(os.path.join(out, "contact-sheet.png"))
print(f"{len(files)} screens -> {out}")
