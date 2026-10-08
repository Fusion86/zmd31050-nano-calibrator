"""Plot endpoint-based calibration predictions; no hardware measurements of new curve."""
from pathlib import Path
import re
import statistics

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

ROOT = Path(__file__).resolve().parents[1]

def mean_count(name):
    return statistics.mean(int(x) for x in re.findall(r"count=(\d+)", (ROOT / name).read_text()))

low = mean_count("pumped.txt")
high = mean_count("vented.txt")
pressure = np.linspace(0, 1, 501)
old_counts = low + pressure * (high - low)

def nonlinear(y):
    return y * (1 - 79 / 32768) + 79 / 32768 * y**2

def inverse(p):
    lo = np.zeros_like(p)
    hi = np.ones_like(p)
    for _ in range(60):
        mid = (lo + hi) / 2
        mask = nonlinear(mid) < p
        lo = np.where(mask, mid, lo)
        hi = np.where(mask, hi, mid)
    return (lo + hi) / 2

# Reconstruct the intermediate raw pressure at normalized raw temperature t=0.
# Evaluate the actual rounded RAM coefficients, rather than an ideal straight line.
y = inverse(old_counts / 32768)
raw_pressure = y * (-5650) - (-4855)
new_counts = nonlinear((raw_pressure - 5011) / -5833) * 32768
old_volts = np.floor(old_counts / 16) * 5 / 2048
new_volts = np.floor(new_counts / 16) * 5 / 2048

fig, (ax, zoom) = plt.subplots(1, 2, figsize=(11, 4.6), gridspec_kw={"width_ratios": [1.6, 1]})
for panel in (ax, zoom):
    panel.plot(pressure, old_volts, label="Old: endpoint-based model", color="#2563eb")
    panel.plot(pressure, new_volts, label="New: predicted RAM calibration", color="#15803d")
    panel.axhline(0.5, color="#dc2626", linestyle="--", label="Software cutoff: 0.500 V")
    panel.set_xlabel("Pressure (bar absolute)")
    panel.set_ylabel("Analog OUT (V)")
    panel.grid(alpha=0.25)
ax.scatter([0, 1], [low * 5 / 32768, high * 5 / 32768], color="#2563eb", s=40, zorder=5)
ax.set_xlim(0, 1)
ax.set_ylim(0.25, 4.7)
ax.set_title("Full pressure range")
ax.legend(loc="upper left", fontsize=8)
zoom.set_xlim(0, 0.05)
zoom.set_ylim(0.35, 0.72)
zoom.set_title("Vacuum endpoint detail")
fig.suptitle("SSIB001AU9AH5: old vs candidate calibration", fontweight="bold")
fig.text(0.5, 0.015, "Assumes 5.000 V VDDA, t=0; old curve interpolated from measured endpoints. New curve requires hardware verification.", ha="center", fontsize=8)
fig.tight_layout(rect=(0, 0.05, 1, 0.93))
for extension in ("png", "svg"):
    fig.savefig(ROOT / "docs" / f"calibration-comparison.{extension}", dpi=180)
print(f"Old endpoint counts: {low:.3f}, {high:.3f}")
print(f"Predicted DAC endpoint V: {new_volts[0]:.6f}, {new_volts[-1]:.6f}")
