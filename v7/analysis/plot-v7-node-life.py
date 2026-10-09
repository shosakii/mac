#!/usr/bin/env python3
"""Plot one radio's full 35-second topology-learning life in Chinese."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


EVENT_LABELS = {
    "NODE_FAIL": "失效",
    "NODE_RECOVER": "恢复",
    "NODE_MOVE": "移动",
}

plt.rcParams.update({
    "font.family": "Microsoft YaHei",
    "axes.unicode_minus": False,
    "svg.fonttype": "none",
    "figure.facecolor": "white",
    "font.size": 11,
})


def load(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def segments(rows: list[dict[str, str]]) -> list[list[dict[str, str]]]:
    """Do not draw a false continuous line through the node's offline period."""
    result: list[list[dict[str, str]]] = []
    current: list[dict[str, str]] = []
    previous = None
    for row in rows:
        time = float(row["time_s"])
        if previous is not None and time - previous > 0.11:
            result.append(current)
            current = []
        current.append(row)
        previous = time
    if current:
        result.append(current)
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("biography", type=Path)
    parser.add_argument("events", type=Path)
    parser.add_argument("--node", type=int, default=27)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = load(args.biography)
    events = load(args.events)
    if not rows:
        raise ValueError("Empty biography")
    fig, axes = plt.subplots(2, 1, figsize=(13, 6.9), sharex=True,
                             gridspec_kw={"hspace": 0.16})
    series = [
        ("one_hop_fp", "一跳误报", "#C95A54"),
        ("one_hop_fn", "一跳漏报", "#D99130"),
        ("two_hop_fp", "二跳误报", "#7B65AB"),
        ("two_hop_fn", "二跳漏报", "#167F83"),
    ]
    for panel, fields in zip(axes, (series[:2], series[2:])):
        for field, label, color in fields:
            first = True
            for part in segments(rows):
                panel.step([float(r["time_s"]) for r in part],
                           [int(r[field]) for r in part], where="post", linewidth=1.7,
                           label=label if first else None, color=color)
                first = False
        panel.grid(axis="y", color="#E1E7EC", linewidth=0.8)
        panel.set_axisbelow(True)
        panel.set_ylabel("错误邻居数量 / 个")
        panel.set_ylim(bottom=-0.1)
        panel.legend(loc="upper right", ncol=2, frameon=False)
        panel.axvspan(4.05, 9.03, facecolor="#DCE1E6", alpha=0.72,
                      label="节点离线")
    axes[0].set_title("一跳邻居表：错误如何出现、持续和消失", loc="left", fontweight="bold")
    axes[1].set_title("二跳邻居表：错误如何出现、持续和消失", loc="left", fontweight="bold")
    for event in events:
        time = float(event["time_s"])
        label = f"{event['node']}号{EVENT_LABELS.get(event['event_type'], event['event_type'])}"
        for ax in axes:
            ax.axvline(time, color="#AAB4BF", linestyle="--", linewidth=0.9)
        axes[0].text(time + 0.12, axes[0].get_ylim()[1] * 0.65, label,
                     rotation=90, va="bottom", fontsize=8, color="#596675")
    axes[1].set_xlabel("仿真时间 / 秒")
    axes[1].set_xlim(0, 35)
    fig.suptitle(f"{args.node} 号电台的一生：与真实拓扑逐时刻对照", x=0.02,
                 y=0.99, ha="left", fontsize=17, fontweight="bold")
    fig.subplots_adjust(top=0.86, bottom=0.17, left=0.08, right=0.98)
    fig.text(0.02, 0.045,
             "灰色区间为本节点失效，期间没有本节点观测记录；误报＝表里有但真实没有，漏报＝真实存在但表里没有。",
             color="#586779", fontsize=10)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=200, bbox_inches="tight")
    fig.savefig(args.output.with_suffix(".svg"), bbox_inches="tight")
    plt.close(fig)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
