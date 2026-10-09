#!/usr/bin/env python3
"""Draw readable Chinese V7 paired-address figures from completed experiments.

The input is ``per-run.csv`` produced by run-v7-robustness.py.  This script
refuses an incomplete A/B/C/D seed so a running matrix cannot be presented as
a finished result.  Each plotted dot is one paired seed difference (test-A),
not one packet, reception, or node treated as an independent replicate.
"""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


MODE_NAMES = {"A": "双标准", "B": "仅信标短", "C": "仅数据短", "D": "信标＋数据短"}
PROFILE_NAMES = {"sparse": "稀疏", "medium": "中等", "dense": "密集"}
COLORS = {"A": "#455A73", "B": "#15827E", "C": "#D27A28", "D": "#7761A8"}
METRICS = {
    "topology_airtime_us": ("控制帧累计空口时间", "ms", 1 / 1000),
    "control_window_attempt_occupancy": ("控制窗尝试占用率", "%", 100),
    "final_exact_both_ratio": ("结束时两表全对节点比例", "%", 100),
    "final_one_hop_f1": ("结束时一跳邻居 F1", "%", 100),
    "final_two_hop_f1": ("结束时二跳邻居 F1", "%", 100),
    "mean_node_lifetime_exact_ratio": ("节点生命期两表全对比例", "%", 100),
    "offered_completion_ratio": ("业务需求成功完成比例", "%", 100),
    "airtime_us_per_delivered_kib": ("每交付 KiB 的数据空口时间", "µs / KiB", 1),
}

plt.rcParams.update({
    "font.family": "Microsoft YaHei",
    "axes.unicode_minus": False,
    "svg.fonttype": "none",
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "font.size": 11,
    "axes.titlesize": 13,
})


def read_paired(paths: list[Path]) -> dict[str, dict[int, dict[str, dict[str, str]]]]:
    rows = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as stream:
            rows.extend(csv.DictReader(stream))
    if not rows:
        raise ValueError(f"No rows in {paths}")
    studies = {row["study"] for row in rows}
    if len(studies) != 1:
        raise ValueError(f"Mixed studies in {paths}: {studies}")
    profiles: dict[str, dict[int, dict[str, dict[str, str]]]] = defaultdict(
        lambda: defaultdict(dict)
    )
    for row in rows:
        profile, seed, mode = row["profile"], int(row["run"]), row["mode"]
        if mode in profiles[profile][seed]:
            if profiles[profile][seed][mode] != row:
                raise ValueError(f"Conflicting result: {profile} seed {seed} mode {mode}")
            continue
        profiles[profile][seed][mode] = row
    for profile, seeds in profiles.items():
        for seed, modes in seeds.items():
            if set(modes) != set(MODE_NAMES):
                raise ValueError(f"Incomplete paired result: {profile} seed {seed}: {sorted(modes)}")
    return dict(profiles)


def save(fig: plt.Figure, out: Path, stem: str) -> None:
    out.mkdir(parents=True, exist_ok=True)
    fig.savefig(out / f"{stem}.png", dpi=200, bbox_inches="tight")
    fig.savefig(out / f"{stem}.svg", bbox_inches="tight")
    plt.close(fig)


def profile_title(profile: str) -> str:
    if profile in PROFILE_NAMES:
        return f"{PROFILE_NAMES[profile]}拓扑"
    if profile.startswith("packet"):
        return f"业务包 {profile.removeprefix('packet')} B"
    if profile.startswith("loss"):
        return f"控制帧丢失 {int(profile.removeprefix('loss'))}%"
    return profile


def plot_absolute(profiles: dict, metric: str, out: Path, stem: str) -> None:
    name, unit, scale = METRICS[metric]
    keys = list(profiles)
    fig, axes = plt.subplots(1, len(keys), figsize=(max(5.1 * len(keys), 6), 5.7),
                             squeeze=False, sharey=True)
    for ax, profile in zip(axes[0], keys):
        seeds = profiles[profile]
        for index, mode in enumerate(MODE_NAMES):
            values = [float(seeds[seed][mode][metric]) * scale for seed in sorted(seeds)]
            n = len(values)
            offsets = [0] if n == 1 else [0.22 * (i / (n - 1) - 0.5) for i in range(n)]
            ax.scatter([index + off for off in offsets], values, s=34,
                       color=COLORS[mode], alpha=0.65, zorder=3)
            mean = statistics.mean(values)
            ax.hlines(mean, index - 0.27, index + 0.27,
                      color=COLORS[mode], linewidth=3.2, zorder=4)
        ax.set_title(f"{profile_title(profile)}（{len(profiles[profile])} 个种子）", fontweight="bold")
        ax.set_xticks(range(4), ["A\n双标准", "B\n仅信标短", "C\n仅数据短", "D\n两者短"])
        ax.grid(axis="y", color="#E1E7EC", linewidth=0.8)
        ax.set_axisbelow(True)
    axes[0][0].set_ylabel(f"{name} / {unit}")
    fig.suptitle(f"{name}：四种地址组合的实测结果", x=0.03, ha="left",
                 fontsize=16, fontweight="bold")
    fig.subplots_adjust(top=0.82, bottom=0.24, wspace=0.11)
    fig.text(0.02, 0.04, "每个圆点＝一个随机种子；横线＝种子均值。A/B/C/D 使用相同的业务需求；纵轴可能局部放大。",
             color="#586779", fontsize=10)
    save(fig, out, stem)


def plot_paired(profiles: dict, metric: str, out: Path, stem: str) -> None:
    name, unit, scale = METRICS[metric]
    keys = list(profiles)
    fig, axes = plt.subplots(1, len(keys), figsize=(max(5.1 * len(keys), 6), 5.7),
                             squeeze=False, sharey=True)
    all_deltas = []
    delta_by_profile = {}
    for profile in keys:
        seeds = profiles[profile]
        delta_by_profile[profile] = {}
        for mode in "BCD":
            deltas = [(float(seeds[seed][mode][metric]) -
                       float(seeds[seed]["A"][metric])) * scale for seed in sorted(seeds)]
            delta_by_profile[profile][mode] = deltas
            all_deltas.extend(deltas)
    limit = max((abs(x) for x in all_deltas), default=0)
    limit = max(limit * 1.28, 0.01 if unit == "%" else 0.1)
    if not math.isfinite(limit):
        raise ValueError(f"Non-finite paired difference for {metric}")
    for ax, profile in zip(axes[0], keys):
        for index, mode in enumerate("BCD"):
            values = delta_by_profile[profile][mode]
            n = len(values)
            offsets = [0] if n == 1 else [0.22 * (i / (n - 1) - 0.5) for i in range(n)]
            ax.scatter([index + off for off in offsets], values, s=38,
                       color=COLORS[mode], alpha=0.7, zorder=3)
            ax.hlines(statistics.mean(values), index - 0.25, index + 0.25,
                      color=COLORS[mode], linewidth=3.2, zorder=4)
        ax.axhline(0, color="#667085", linewidth=1)
        ax.set_title(f"{profile_title(profile)}（{len(profiles[profile])} 个种子）", fontweight="bold")
        ax.set_xticks(range(3), ["B－A\n仅信标短", "C－A\n仅数据短", "D－A\n两者短"])
        ax.set_ylim(-limit, limit)
        ax.grid(axis="y", color="#E1E7EC", linewidth=0.8)
        ax.set_axisbelow(True)
    axes[0][0].set_ylabel(f"与双标准 A 的配对差值 / {unit}")
    fig.suptitle(f"短地址带来的变化：{name}", x=0.03, ha="left",
                 fontsize=16, fontweight="bold")
    fig.subplots_adjust(top=0.82, bottom=0.26, wspace=0.11)
    fig.text(0.02, 0.04, "每个圆点＝同一种子下测试组减 A；横线＝本密度种子平均差值。低于零不一定对所有指标都更好。",
             color="#586779", fontsize=10)
    save(fig, out, stem)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("per_run", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    profiles = read_paired(args.per_run)
    prefix = next(iter(profiles.values()))
    study = next(iter(next(iter(prefix.values())).values()))["study"]
    out = args.output / study
    plot_absolute(profiles, "final_exact_both_ratio", out, "01-结束时两表全对")
    plot_absolute(profiles, "mean_node_lifetime_exact_ratio", out, "02-节点生命期正确率")
    plot_absolute(profiles, "offered_completion_ratio", out, "03-业务成功完成率")
    plot_paired(profiles, "topology_airtime_us", out, "04-控制帧空口时间差")
    plot_paired(profiles, "airtime_us_per_delivered_kib", out, "05-数据空口效率差")
    plot_paired(profiles, "mean_node_lifetime_exact_ratio", out, "06-节点生命期正确率差")
    print(f"Created 6 PNG and 6 SVG figures from {len(profiles)} completed profiles in {out}")


if __name__ == "__main__":
    main()
