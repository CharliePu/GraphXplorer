# Precision audit for equality renders against a brute-force ground truth.
#
#   build-release\engine\gxrender.exe "sin(x)*sin(y) = sin(x*y)" out\audit_eq.png 0 0 0.17 512
#   python out\audit_equality.py out\audit_eq.png 0 0 0.17
#
# The formula is hard-coded below (must be numpy-expressible). Per pixel, an
# 8x8 subgrid sign test decides "a curve truly crosses this pixel". Reported:
#   - misses: curve pixels we render dark; "orphan" = no lit neighbor either
#     (a strand that would be INVISIBLE -- the real precision violation)
#   - fabrications: lit pixels with no curve within 1px (beyond the AA skirt),
#     with their alpha distribution (sub-0.5 = soft halo, ~1.0 = false claim)
import sys
import numpy as np
from PIL import Image

f_true = lambda x, y: np.sin(x) * np.sin(y) - np.sin(x * y)

path, cx, cy, wpp = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
img = np.asarray(Image.open(path).convert("L"), dtype=np.float64) / 255.0
N = img.shape[0]
half = N / 2 * wpp
ys = cy + (np.arange(N)[::-1] + 0.5) * wpp - half
xs = cx + (np.arange(N) + 0.5) * wpp - half

S = 8
sub = (np.arange(S) + 0.5) / S - 0.5
gx = xs[None, :, None, None] + sub[None, None, :, None] * wpp
gy = ys[:, None, None, None] + sub[None, None, None, :] * wpp
pos = (f_true(gx, gy) > 0).reshape(N, N, -1)
truth = pos.any(axis=2) & ~pos.all(axis=2)
ours = img[::-1, :]
lit = ours > 0.10

def dilate(m):
    out = np.zeros_like(m)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out

fab = lit & ~dilate(truth)
miss = ~lit & truth
orphan = miss & ~dilate(lit)
print(f"pixels {N*N}, truth-has-curve {truth.mean()*100:.1f}%")
print(f"misses {miss.sum()}  orphan (invisible strand!) {orphan.sum()}")
print(f"fabrications {fab.sum()} ({fab.sum()/ours.size*100:.3f}%)", end="")
if fab.sum():
    print(f"  alpha median {np.median(ours[fab]):.2f} max {ours[fab].max():.2f}", end="")
print()
sys.exit(1 if orphan.sum() or (fab.sum() and ours[fab].max() > 0.6) else 0)
