#!/usr/bin/env python3
"""Generate a long-term drift chart from ``longterm.txt`` output."""

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DATA_PATH = ROOT / "longterm.txt"
OUT_PATH = ROOT / "assets" / "longterm_drift.png"


def parse_data(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Parse current 9-column longterm output.

    The format keeps separate steady-clock endpoints for each backend::
        sec static(ns) online(ns) st_std(ns) on_std(ns) st_drift st_ppm on_drift on_ppm
    """
    values: list[tuple[int, float, float]] = []

    with path.open() as data_file:
        for line_number, raw_line in enumerate(data_file, 1):
            line = raw_line.strip()
            if not line or line.startswith("-"):
                continue
            if line.startswith(("Long", "TSC", "sec")):
                continue

            parts = line.split()
            if not parts[0].lstrip("+-").isdigit():
                # Preamble or an unrelated comment, not a data row.
                continue
            if len(parts) != 9:
                raise ValueError(
                    f"{path}:{line_number}: expected 9 columns, "
                    f"got {len(parts)}"
                )

            try:
                second = int(parts[0])
                static_ppm = float(parts[6])
                online_ppm = float(parts[8])
            except ValueError as error:
                raise ValueError(
                    f"{path}:{line_number}: invalid numeric data row: {line}"
                ) from error

            if not np.isfinite(static_ppm) or not np.isfinite(online_ppm):
                raise ValueError(
                    f"{path}:{line_number}: drift values must be finite"
                )
            values.append((second, static_ppm, online_ppm))

    if not values:
        raise RuntimeError(f"No valid 9-column data rows found in {path}")

    parsed = np.array(values, dtype=float)
    return parsed[:, 0], parsed[:, 1], parsed[:, 2]


secs, st_ppm, on_ppm = parse_data(DATA_PATH)

OUT_PATH.parent.mkdir(parents=True, exist_ok=True)

fig, ax = plt.subplots(figsize=(12, 5))
ax.plot(secs, st_ppm, linewidth=0.8, color="#2563eb", label="static_clock")
ax.plot(secs, on_ppm, linewidth=0.8, color="#dc2626", alpha=0.85, label="online")

ax.axhline(y=0, color="#6b7280", linewidth=0.5, linestyle="--")
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
    f"({len(secs)} s, static/online each compared with its steady_clock endpoint)",
    fontsize=10,
    color="#6b7280",
    pad=6,
    loc="center",
)
ax.legend(loc="lower right", fontsize=10, framealpha=0.9)
ax.grid(True, alpha=0.3)

# Keep annotations inside a sensible area even for short captures.
x_span = max(float(secs[-1] - secs[0]), 1.0)
label_x = float(secs[-1]) - 0.18 * x_span
ax.annotate(
    f"static final: {st_ppm[-1]:+.4f} ppm",
    xy=(secs[-1], st_ppm[-1]),
    xytext=(label_x, st_ppm[-1] + 0.06),
    fontsize=8.5,
    color="#2563eb",
    arrowprops=dict(arrowstyle="->", color="#2563eb", lw=0.8),
)
ax.annotate(
    f"online final: {on_ppm[-1]:+.4f} ppm",
    xy=(secs[-1], on_ppm[-1]),
    xytext=(label_x, on_ppm[-1] - 0.12),
    fontsize=8.5,
    color="#dc2626",
    arrowprops=dict(arrowstyle="->", color="#dc2626", lw=0.8),
)

fig.tight_layout()
fig.savefig(OUT_PATH, dpi=150, bbox_inches="tight")
print(f"Saved chart to {OUT_PATH}")
print(
    f"Data points: {len(secs)}, static_ppm range: "
    f"[{st_ppm.min():.4f}, {st_ppm.max():.4f}], online_ppm range: "
    f"[{on_ppm.min():.4f}, {on_ppm.max():.4f}]"
)
