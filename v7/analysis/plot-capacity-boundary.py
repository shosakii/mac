#!/usr/bin/env python3
"""Plot the V6 K=8..12 control-window boundary."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("aggregate_csv", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    rows = list(csv.DictReader(args.aggregate_csv.open(newline="", encoding="utf-8")))
    output = args.output or args.aggregate_csv.with_name("capacity-boundary-v6.png")
    fig, axes = plt.subplots(1, 2, figsize=(11.5, 4.3), sharex=True)
    styles = {"standard": ("o", "#2474a6"), "short": ("s", "#d95f59")}
    for address, (marker, color) in styles.items():
        group = sorted((row for row in rows if row["beacon_address"] == address),
                       key=lambda row: int(row["control_contenders"]))
        k = [int(row["control_contenders"]) for row in group]
        rx = [float(row["mean_receiver_delivery"]) for row in group]
        exact = [float(row["mean_final_exact_both_ratio"]) for row in group]
        axes[0].plot(k, rx, marker=marker, color=color, label=address)
        axes[1].plot(k, exact, marker=marker, color=color, label=address)
    axes[0].axhline(0.99, color="0.35", ls="--", lw=1, label="safety threshold")
    axes[0].set_title("Control-frame receiver delivery")
    axes[0].set_ylabel("Mean ratio")
    axes[1].set_title("Nodes with both topology tables exact")
    for ax in axes:
        ax.set_xlabel("Beacon contenders K per 10-ms control window")
        ax.set_xticks([8, 9, 10, 11, 12])
        ax.set_ylim(0, 1.04)
        ax.grid(alpha=0.25)
        ax.legend()
    fig.suptitle("V6 refined capacity boundary: K=8 is safe; K=9 already overloads")
    fig.tight_layout()
    fig.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
