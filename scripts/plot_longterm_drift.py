#!/usr/bin/env python3
"""Generate long-term drift line chart from longterm.txt data."""

import matplotlib
matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_PATH = ROOT / "longterm.txt"
OUT_PATH = ROOT / "assets" / "longterm_drift.png"

# Parse the data file
# Column layout (8 fields normally):
#   sec  static_ns  online_ns  std_ns  st_drift  st_ppm  on_drift  on_ppm
# After sec >= 100, online_ns and std_ns (each 12 digits) sometimes fuse
# into a single 24-digit field, yielding 7 space-separated fields instead of 8.
secs, st_ppm, on_ppm = [], [], []
with open(DATA_PATH) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("-") or line.startswith("Long") or line.startswith("TSC") or line.startswith("sec"):
            continue
        parts = line.split()
        n = len(parts)
        try:
            secs.append(int(parts[0]))
            if n == 8:
                # Normal line: 8 fields
                st_ppm.append(float(parts[5]))
                on_ppm.append(float(parts[7]))
            elif n == 7:
                # Fused line: online_ns + std_ns merged into one field
                st_ppm.append(float(parts[4]))
                on_ppm.append(float(parts[6]))
            else:
                continue
        except (ValueError, IndexError):
            continue

if not secs:
    raise RuntimeError(f"No data parsed from {DATA_PATH}")

secs = np.array(secs)
st_ppm = np.array(st_ppm)
on_ppm = np.array(on_ppm)

# Create the chart
fig, ax = plt.subplots(figsize=(12, 5))

ax.plot(secs, st_ppm, linewidth=0.8, color="#2563eb", label="static_clock")
ax.plot(secs, on_ppm, linewidth=0.8, color="#dc2626", alpha=0.85, label="online")

# Reference line at zero drift
ax.axhline(y=0, color="#6b7280", linewidth=0.5, linestyle="--")

# Shade the ±0.15 ppm zone mentioned in the README
ax.axhspan(-0.15, 0.15, alpha=0.06, color="#2563eb", label=r"$\pm$0.15 ppm")

ax.set_xlabel("Time (seconds)", fontsize=11)
ax.set_ylabel("Drift (ppm)", fontsize=11)

fig.suptitle(
    "Long-Term Clock Drift: static_clock vs online",
    fontsize=14,
    fontweight="bold",
    y=0.97,
)
ax.set_title(
    f"({len(secs)} s, x86-64 Linux, calibrated against std::chrono::steady_clock)",
    fontsize=10,
    color="#6b7280",
    pad=6,
    loc="center",
)
ax.legend(loc="lower right", fontsize=10, framealpha=0.9)
ax.grid(True, alpha=0.3)

# Add annotations — place them on opposite sides to avoid overlap
ax.annotate(
    f"static_clock final: {st_ppm[-1]:+.4f} ppm",
    xy=(secs[-1], st_ppm[-1]),
    xytext=(secs[-1] - 50, st_ppm[-1] + 0.06),
    fontsize=8.5,
    color="#2563eb",
    arrowprops=dict(arrowstyle="->", color="#2563eb", lw=0.8),
)
ax.annotate(
    f"online final: {on_ppm[-1]:+.4f} ppm",
    xy=(secs[-1], on_ppm[-1]),
    xytext=(secs[-1] - 50, on_ppm[-1] - 0.12),
    fontsize=8.5,
    color="#dc2626",
    arrowprops=dict(arrowstyle="->", color="#dc2626", lw=0.8),
)

fig.tight_layout()
fig.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
print(f"Saved chart to {OUT_PATH}")
print(f"Data points: {len(secs)}, static_ppm range: [{st_ppm.min():.4f}, {st_ppm.max():.4f}], online_ppm range: [{on_ppm.min():.4f}, {on_ppm.max():.4f}]")
