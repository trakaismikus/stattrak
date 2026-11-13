"""Finish the render of render_device.py: a fine grain on the metal, then a WebP file.

    python tools/device_art/finish_render.py render.png updater/assets/device.webp

The renderer's noise filter removes the fine grain of the bead blasted metal. This adds it again:
a little luminance noise, only on the metal (not on the black glass, not on the etched text).
Needs Pillow and NumPy.
"""
import sys

import numpy as np
from PIL import Image, ImageFilter

AMOUNT = 2.2


def main(src, dst):
    im = np.array(Image.open(src).convert('RGBA')).astype(np.float32)
    rng = np.random.default_rng(7)
    noise = rng.normal(0, 1, im.shape[:2]).astype(np.float32)
    noise = Image.fromarray(((noise * 40) + 128).clip(0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
    noise = np.array(noise).astype(np.float32) / 40 - 3.2
    lum = im[..., :3].mean(-1)
    metal = ((lum > 14) & (lum < 120) & (im[..., 3] > 250)).astype(np.float32)
    for c in range(3):
        im[..., c] += noise * AMOUNT * metal
    Image.fromarray(im.clip(0, 255).astype(np.uint8)).save(dst, quality=90, method=6)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
