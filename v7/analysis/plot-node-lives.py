#!/usr/bin/env python3
"""Plot V6 whole-life evidence for presentations."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


ROLE_NAMES = {
    "worst_lifetime": "Worst lifetime",
    "median_lifetime": "Median lifetime",
    "most_one_hop_affected": "Most 1-hop affected",
    "most_two_hop_only_affected": "2-hop-only affected",
    "event_subject": "Event subject",
    "stable_control": "Stable control",
}


def read_rows(path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def save(fig, output, name):
    fig.tight_layout()
    fig.savefig(output / name, dpi=180, bbox_inches="tight")
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("experiment_dir", type=Path)
    parser.add_argument("--case", default="random-seed61")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.experiment_dir.resolve()
    case = root / args.case
    output = (args.output or root / "figures").resolve()
    output.mkdir(parents=True, exist_ok=True)

    all_nodes = read_rows(root / "all-node-lifetimes.csv")
    runs = sorted({int(row["run"]) for row in all_nodes})
    fig, ax = plt.subplots(figsize=(9.2, 4.8))
    values = [[float(r["post_cold_exact_both_ratio"]) for r in all_nodes
               if int(r["run"]) == run] for run in runs]
    ax.boxplot(values, labels=[str(run) for run in runs], showmeans=True)
    for x, group in enumerate(values, 1):
        jitter = np.linspace(-0.12, 0.12, len(group))
        ax.scatter(x + jitter, group, s=9, alpha=0.45, color="#3b78a8")
    ax.set_xlabel("Random run")
    ax.set_ylabel("Post-cold exact 1-hop + 2-hop lifetime ratio")
    ax.set_ylim(0, 1.02)
    ax.grid(axis="y", alpha=0.25)
    ax.set_title("Every radio is evaluated over its whole life (64 radios per run)")
    save(fig, output, "01_all-node-lifetime-distribution.png")

    reps = read_rows(case / "node-life-report/representative-nodes.csv")
    events = read_rows(case / "topology-events-ground-truth.csv")
    fig, axes = plt.subplots(3, 2, figsize=(12.5, 10.0), sharex=True)
    for ax, rep in zip(axes.flat, reps):
        node = int(rep["observer"])
        trace = read_rows(case / f"node-life-report/node-{node:02d}/topology-timeseries.csv")
        time = [float(r["time_s"]) for r in trace]
        one_error = [int(r["one_hop_fp"]) + int(r["one_hop_fn"]) for r in trace]
        two_error = [int(r["two_hop_fp"]) + int(r["two_hop_fn"]) for r in trace]
        ax.step(time, one_error, where="post", label="1-hop errors", color="#2474a6")
        ax.step(time, two_error, where="post", label="2-hop errors", color="#d95f59")
        for event in events:
            ax.axvline(float(event["time_s"]), color="0.55", ls=":", lw=0.7)
        role = ROLE_NAMES.get(rep["role"], rep["role"])
        ax.set_title(f"{role}: radio {node}")
        ax.set_ylabel("Wrong entries")
        ax.grid(alpha=0.2)
    axes[-1, 0].set_xlabel("Experiment time (s)")
    axes[-1, 1].set_xlabel("Experiment time (s)")
    axes[0, 0].legend(loc="upper right", fontsize=8)
    fig.suptitle(f"Rule-selected representative radio lives: {args.case}", y=1.01)
    save(fig, output, "02_representative-radio-lives.png")

    labels = [ROLE_NAMES.get(row["role"], row["role"]) for row in reps]
    ratios = [float(row["post_cold_exact_both_ratio"]) for row in reps]
    max_error = [float(row["max_continuous_error_s"]) for row in reps]
    fig, (left, right) = plt.subplots(1, 2, figsize=(12.5, 4.6))
    y = np.arange(len(labels))
    left.barh(y, ratios, color="#4c9f70")
    left.set_yticks(y, labels=labels)
    left.invert_yaxis()
    left.set_xlim(0, 1.02)
    left.set_xlabel("Exact-topology lifetime ratio")
    left.grid(axis="x", alpha=0.2)
    right.barh(y, max_error, color="#d9834b")
    right.set_yticks(y, labels=[])
    right.invert_yaxis()
    right.set_xlabel("Longest continuous wrong state (s)")
    right.grid(axis="x", alpha=0.2)
    fig.suptitle("Representative-node whole-life scorecard")
    save(fig, output, "03_representative-scorecard.png")

    detections = read_rows(root / "all-event-detections.csv")
    fig, ax = plt.subplots(figsize=(9.2, 4.8))
    for hop, marker, color in (("1", "o", "#2474a6"), ("2", "^", "#d95f59")):
        rows = [row for row in detections if row["hop"] == hop]
        x = [float(row["event_time_s"]) + (int(row["run"]) - min(runs)) * 0.018 for row in rows]
        y = [float(row["stable_correct_delay_s"]) for row in rows]
        ok_x = [vx for vx, vy in zip(x, y) if vy >= 0]
        ok_y = [vy for vy in y if vy >= 0]
        ax.scatter(ok_x, ok_y, s=15, alpha=0.45, marker=marker, color=color,
                   label=f"{hop}-hop affected observer")
    ax.set_xlabel("Topology event time (s; slight run jitter for visibility)")
    ax.set_ylabel("Stable-correct delay (s)")
    ax.set_title("Recovery time of every affected observer after every event")
    ax.grid(alpha=0.2)
    ax.legend()
    save(fig, output, "04_all-event-recovery-delays.png")
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
