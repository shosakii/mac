#!/usr/bin/env python3
"""Compact, question-led Chinese figures for the V7 teacher briefing.

The plots deliberately separate serialized frame size, topology quality,
control-window cost, and paired business outcomes.  They never pool packets as
independent replications: each business dot is one paired simulation seed.
"""

from __future__ import annotations

import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


BASE = Path(__file__).resolve().parents[1]
RESULTS = BASE / "results" / "robustness-density-staggered"
OUT = Path(__file__).resolve().parent / "figures" / "robustness-clear"
INK = "#21334A"
MUTED = "#586779"
GRID = "#DDE5EB"
BLUE = "#255B86"
TEAL = "#168079"
ORANGE = "#C77928"
PURPLE = "#7460A6"
SOFT = "#E7F0EF"

plt.rcParams.update({
    "font.family": "Microsoft YaHei",
    "axes.unicode_minus": False,
    "svg.fonttype": "none",
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.edgecolor": GRID,
    "axes.labelcolor": INK,
    "xtick.color": MUTED,
    "ytick.color": INK,
    "text.color": INK,
    "font.size": 12,
    "axes.titlesize": 17,
})


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def evidence() -> dict[tuple[str, int, str], dict[str, str]]:
    combined: dict[tuple[str, int, str], dict[str, str]] = {}
    for path in (RESULTS / "per-run-sparse-snapshot.csv", RESULTS / "per-run.csv"):
        for row in read_csv(path):
            key = row["profile"], int(row["run"]), row["mode"]
            if key in combined and combined[key] != row:
                raise ValueError(f"Conflicting paired run: {key}")
            combined[key] = row
    expected = {(p, s, m)
                for p, seeds in (("sparse", range(81, 86)),
                                 ("medium", range(81, 84)),
                                 ("dense", range(81, 84)))
                for s in seeds for m in "ABCD"}
    if set(combined) != expected:
        raise ValueError(f"Expected 44 runs; missing={expected-set(combined)}, "
                         f"extra={set(combined)-expected}")
    return combined


def save(fig: plt.Figure, name: str) -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT / f"{name}.png", dpi=210, bbox_inches="tight")
    fig.savefig(OUT / f"{name}.svg", bbox_inches="tight")
    plt.close(fig)


def frame_table(kind: str) -> None:
    beacon = kind == "beacon"
    fields = ["帧控制", "时长", "地址1", "地址2", "地址3", "序列"]
    standard = [2, 2, 6, 6, 6, 2]
    short = [2, 2, 2, 2, 2, 2]
    if not beacon:
        fields.append("QoS")
        standard.append(2)
        short.append(2)
    fig, ax = plt.subplots(figsize=(8.4, 3.4))
    ax.set_xlim(-1.1, len(fields) + 0.3)
    ax.set_ylim(-0.65, 2.9)
    ax.axis("off")
    for j, label in enumerate(fields):
        ax.text(j + 0.5, 2.47, label, ha="center", va="center",
                fontsize=12, fontweight="medium")
        for y, value in ((1.35, standard[j]), (0.35, short[j])):
            changed = 2 <= j <= 4
            ax.add_patch(Rectangle((j + 0.05, y), 0.9, 0.7,
                                   facecolor=SOFT if changed else "#EEF1F5",
                                   edgecolor="white", linewidth=1.5))
            ax.text(j + 0.5, y + 0.35, f"{value} B", ha="center", va="center",
                    fontsize=14, color=TEAL if changed else INK,
                    fontweight="bold" if changed else "normal")
    ax.text(-0.15, 1.7, "标准", ha="right", va="center", fontsize=13)
    ax.text(-0.15, 0.7, "短地址", ha="right", va="center", fontsize=13)
    old, new = sum(standard), sum(short)
    frame = "Beacon" if beacon else "QoS Data"
    fig.suptitle(f"{frame}：只有三个地址字段缩短", x=0.04, y=1.02,
                 ha="left", fontsize=18, fontweight="bold")
    fig.text(0.04, 0.04,
             f"MAC 首部 {old} → {new} B（每帧 −12 B）。这是首部结构，不是完整{frame}帧；帧体与 FCS 未改。",
             fontsize=11, color=MUTED)
    fig.subplots_adjust(top=0.83, bottom=0.18)
    save(fig, "01-beacon-header" if beacon else "02-data-header")


def life_summary(data: dict) -> None:
    profiles = [("稀疏", "sparse", range(81, 86)),
                ("中等", "medium", range(81, 84)),
                ("密集", "dense", range(81, 84))]
    values = [100 * statistics.mean(
        float(data[p, seed, "A"]["mean_node_lifetime_exact_ratio"]) for seed in seeds)
              for _, p, seeds in profiles]
    fig, ax = plt.subplots(figsize=(8.1, 4.0))
    ys = [2, 1, 0]
    ax.barh(ys, values, color=TEAL, height=0.45)
    for y, value in zip(ys, values):
        ax.text(value + 0.7, y, f"{value:.2f}%", va="center", fontsize=13,
                fontweight="bold", ha="left")
    ax.set_yticks(ys, [f"{name}拓扑" for name, _, _ in profiles])
    ax.set_xlim(0, 108)
    ax.set_xticks([0, 25, 50, 75, 100])
    ax.set_xlabel("两表同时完全正确的比例 / %")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    ax.set_title("首次全对后，仍有短时态势错误", loc="left", fontweight="bold")
    fig.subplots_adjust(bottom=0.25, top=0.84, left=0.16)
    fig.text(0.02, 0.035,
             "柱长＝首次全对之后，两表仍同时正确的在线时间占比；44 次运行在结束时均全对。稀疏 5 种子，中等／密集各 3 种子。",
             fontsize=10.5, color=MUTED)
    save(fig, "03-topology-life")


def control_cost(data: dict) -> None:
    fig, ax = plt.subplots(figsize=(8.1, 3.6))
    names = [("中等拓扑", "medium", range(81, 84), 1),
             ("密集拓扑", "dense", range(81, 84), 0)]
    for name, profile, seeds, y in names:
        standard = 100 * statistics.mean(
            float(data[profile, seed, "A"]["control_window_attempt_occupancy"])
            for seed in seeds)
        short = 100 * statistics.mean(
            float(data[profile, seed, "B"]["control_window_attempt_occupancy"])
            for seed in seeds)
        ax.plot([short, standard], [y, y], color=GRID, linewidth=7,
                solid_capstyle="round", zorder=1)
        ax.scatter([standard], [y], color=BLUE, s=100, zorder=3)
        ax.scatter([short], [y], color=TEAL, s=100, zorder=3)
        ax.text(standard + 0.26, y + 0.07, f"标准 {standard:.2f}%",
                va="bottom", fontsize=11, color=INK)
        ax.text(short - 0.24, y - 0.08, f"短地址 {short:.2f}%",
                va="top", ha="right", fontsize=11, color=INK)
    ax.set_yticks([1, 0], ["中等拓扑", "密集拓扑"])
    ax.set_xlim(0, 19.5)
    ax.set_ylim(-0.65, 1.65)
    ax.set_xlabel("10 ms 控制窗的尝试占用比例 / %")
    ax.set_title("只缩 Beacon：控制窗占用少 1.2745 个百分点", loc="left",
                 fontweight="bold")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.27, top=0.84, left=0.17)
    fig.text(0.02, 0.035,
             "每个密度比较相同种子的 A 与 B；短地址每个 Beacon 少 12 B。纵轴从 0 开始。",
             fontsize=10.5, color=MUTED)
    save(fig, "04-control-window")


def packet_share() -> None:
    sizes = [("256 B 业务", 294), ("1024 B 业务", 1062),
             ("1472 B 业务", 1510)]
    fractions = [12 / total * 100 for _, total in sizes]
    fig, ax = plt.subplots(figsize=(8.1, 3.6))
    ys = [2, 1, 0]
    ax.barh(ys, fractions, color=TEAL, height=0.48)
    for y, (label, total), fraction in zip(ys, sizes, fractions):
        ax.text(fraction + 0.08, y, f"{fraction:.2f}%  （{total}→{total-12} B）",
                va="center", fontsize=12)
    ax.set_yticks(ys, [label for label, _ in sizes])
    ax.set_xlim(0, 5.6)
    ax.set_xlabel("缩短 12 B 占整个 DATA MPDU 的比例 / %")
    ax.set_title("同样少 12 B，大包的相对收益更小", loc="left", fontweight="bold")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.25, top=0.84, left=0.18)
    fig.text(0.02, 0.035,
             "这是 PHY 实测帧长的算术比例，不是成功交付率或系统吞吐量的提升比例。",
             fontsize=10.5, color=MUTED)
    save(fig, "05-data-size-share")


def business_delta(data: dict, profile: str) -> None:
    modes = [("B 只短信标", "B", TEAL),
             ("C 只短数据", "C", ORANGE),
             ("D 两者都短", "D", PURPLE)]
    seeds = range(81, 84)
    rows = []
    for label, mode, color in modes:
        values = [100 * (float(data[profile, seed, mode]["offered_completion_ratio"])
                         - float(data[profile, seed, "A"]["offered_completion_ratio"]))
                  for seed in seeds]
        rows.append((label, color, values, statistics.mean(values)))
    fig, ax = plt.subplots(figsize=(8.1, 4.15))
    ax.axvline(0, color=INK, linewidth=1.2)
    for y, (label, color, values, mean) in zip([2, 1, 0], rows):
        for offset, value in zip([0.17, 0, -0.17], values):
            ax.scatter(value, y + offset, s=50, color=color, alpha=0.65,
                       zorder=3)
        ax.scatter(mean, y, marker="D", s=95, color=INK, zorder=4)
        ax.text(4.05, y, f"均值 {mean:+.2f} 点", ha="right", va="center",
                fontsize=11.5)
    ax.set_yticks([2, 1, 0], [item[0] for item in rows])
    ax.set_xlim(-4.25, 4.25)
    ax.set_xticks([-4, -2, 0, 2, 4])
    ax.set_ylim(-0.55, 2.55)
    ax.set_xlabel("相对 A 组的业务需求完成率差 / 百分点")
    ax.set_title(f"{'中等' if profile=='medium' else '密集'}拓扑：业务结果没有稳定同向改善",
                 loc="left", fontweight="bold")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.25, top=0.84, left=0.20, right=0.98)
    fig.text(0.02, 0.035,
             "每行 3 个彩点＝相同种子下测试组－A；黑菱形＝均值。跨零表示有的种子改善、有的变差。",
             fontsize=10.5, color=MUTED)
    save(fig, f"06-business-{profile}")


def node_life() -> None:
    rows = read_csv(RESULTS / "medium-run81-A-node-27-biography.csv")
    events = read_csv(RESULTS / "medium-run81-A-events.csv")
    fig, ax = plt.subplots(figsize=(9.0, 4.4))
    series = [
        ("一跳错误数", "one_hop_fp", "one_hop_fn", BLUE),
        ("二跳错误数", "two_hop_fp", "two_hop_fn", ORANGE),
    ]
    for label, fp, fn, color in series:
        parts = []
        current = []
        previous = None
        for row in rows:
            time = float(row["time_s"])
            if previous is not None and time - previous > 0.11:
                parts.append(current)
                current = []
            current.append(row)
            previous = time
        if current:
            parts.append(current)
        for j, part in enumerate(parts):
            ax.step([float(r["time_s"]) for r in part],
                    [int(r[fp]) + int(r[fn]) for r in part], where="post",
                    color=color, linewidth=2.0, label=label if j == 0 else None)
    ax.axvspan(4.05, 9.03, color="#E1E4E8", alpha=0.8)
    for event in events:
        time = float(event["time_s"])
        kind = {"NODE_FAIL": "失效", "NODE_RECOVER": "恢复", "NODE_MOVE": "移动"}[event["event_type"]]
        ax.axvline(time, color="#A9B6C2", linestyle="--", linewidth=0.8)
        ax.text(time + 0.10, 13.5, f"{event['node']}号{kind}", rotation=90,
                fontsize=9, va="top", color=MUTED)
    ax.set_xlim(0, 35)
    ax.set_ylim(0, 17)
    ax.set_yticks([0, 4, 8, 12, 16])
    ax.set_xlabel("仿真时间 / 秒")
    ax.set_ylabel("错误邻居数量 / 个")
    ax.set_title("27 号电台：终点正确，不代表整个生命期正确", loc="left",
                 fontweight="bold")
    ax.legend(loc="upper right", ncol=2, frameon=False)
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.23, top=0.82, left=0.09)
    fig.text(0.02, 0.035,
             "错误数＝误报＋漏报；灰区本节点离线、没有观测。取中等拓扑／种子81／A组，每 10 ms 对照真实拓扑。",
             fontsize=10.5, color=MUTED)
    save(fig, "07-node27-life")


def observation_delay() -> None:
    """Use sampled node-27 records, not an invented continuous trace."""
    rows = read_csv(RESULTS / "medium-run81-A-node-27-biography.csv")
    start = 14.07
    before = next(r for r in rows if r["time_s"] == "14.06")
    after = next(r for r in rows if r["time_s"] == "14.07")
    if (before["exact_one_hop"], before["exact_two_hop"]) != ("1", "1"):
        raise ValueError("Node 27 was not correct immediately before event")
    if (after["exact_one_hop"], after["exact_two_hop"]) != ("0", "0"):
        raise ValueError("Node 27 did not become incorrect at event")
    first_one = next(float(r["time_s"]) for r in rows
                     if start < float(r["time_s"]) < 19.04 and
                     r["exact_one_hop"] == "1")
    first_two = next(float(r["time_s"]) for r in rows
                     if start < float(r["time_s"]) < 19.04 and
                     r["exact_two_hop"] == "1")
    fig, ax = plt.subplots(figsize=(8.0, 3.5))
    ax.broken_barh([(start, first_one - start)], (1.42, 0.43),
                   facecolors=BLUE)
    ax.broken_barh([(start, first_two - start)], (0.42, 0.43),
                   facecolors=ORANGE)
    ax.axvline(start, color=INK, linewidth=1.4)
    ax.scatter([first_one, first_two], [1.635, 0.635],
               color=[BLUE, ORANGE], s=65, zorder=3)
    ax.text(first_one + 0.04, 1.635,
            f"{first_one:.2f} s（约 {first_one-start:.2f} s）",
            va="center", fontsize=11.5)
    ax.text(first_two + 0.04, 0.635,
            f"{first_two:.2f} s（约 {first_two-start:.2f} s）",
            va="center", fontsize=11.5)
    ax.text(start + 0.02, 2.08, "14.07 s：36 号移动，真实拓扑改变",
            fontsize=11.5)
    ax.set_yticks([1.635, 0.635], ["27 号一跳表错误", "27 号二跳表错误"])
    ax.set_xlim(13.9, 17.0)
    ax.set_ylim(0.10, 2.42)
    ax.set_xticks([14.0, 14.5, 15.0, 15.5, 16.0, 16.5, 17.0])
    ax.set_xlabel("仿真时间 / 秒")
    ax.set_title("真实拓扑已变 ≠ 电台立即知道：27 号的恢复等待",
                 loc="left", fontweight="bold")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.28, top=0.82, left=0.20, right=0.95)
    fig.text(0.02, 0.035,
             "色带＝采样记录中的错误区间；圆点＝首次采到正确。10 ms 采样使恢复时刻存在至多约 10 ms 的读数误差。",
             fontsize=10.5, color=MUTED)
    save(fig, "08-node27-detection-delay")


def main() -> None:
    data = evidence()
    frame_table("beacon")
    frame_table("data")
    life_summary(data)
    control_cost(data)
    packet_share()
    business_delta(data, "medium")
    business_delta(data, "dense")
    node_life()
    observation_delay()
    print(f"Wrote 9 compact PNG/SVG figures to {OUT}")


if __name__ == "__main__":
    main()
