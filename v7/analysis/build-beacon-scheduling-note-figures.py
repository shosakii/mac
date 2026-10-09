#!/usr/bin/env python3
"""Two legible Chinese figures for the V7 Beacon release-time note."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "analysis" / "figures" / "beacon-scheduling-note"
OUT.mkdir(parents=True, exist_ok=True)

font_path = Path("C:/Windows/Fonts/NotoSansSC-VF.ttf")
if not font_path.exists():
    font_path = Path("C:/Windows/Fonts/msyh.ttc")
font_manager.fontManager.addfont(str(font_path))
plt.rcParams.update({
    "font.family": font_manager.FontProperties(fname=str(font_path)).get_name(),
    "axes.unicode_minus": False,
    "font.size": 13,
    "axes.titlesize": 18,
    "axes.labelsize": 14,
    "xtick.labelsize": 12,
    "ytick.labelsize": 13,
    "savefig.facecolor": "white",
})

BLUE = "#2878A7"
ORANGE = "#D46A32"
INK = "#1C2937"
GRID = "#DCE4EA"


def save(fig: plt.Figure, stem: str) -> None:
    fig.savefig(OUT / f"{stem}.png", dpi=180, bbox_inches="tight", pad_inches=0.2)
    fig.savefig(OUT / f"{stem}.svg", bbox_inches="tight", pad_inches=0.2)
    plt.close(fig)


def timeline() -> None:
    fig, axes = plt.subplots(2, 1, figsize=(12.0, 7.4), sharex=True)
    fig.suptitle("同样每窗 8 台：启动时刻不同，竞争形态就不同", color=INK, y=0.99)
    sequences = [([0.2] * 8, "原 V7：8 台都在 0.2 ms 提出发送请求", BLUE),
                 ([0.2 + 0.9 * i for i in range(8)],
                  "V4 式本地错开：0.2～6.5 ms 依次释放", ORANGE)]
    for ax, (times, title, color) in zip(axes, sequences):
        for i, time_ms in enumerate(times):
            row = 8 - i
            ax.hlines(row, 0, 10, color="#F0F3F6", linewidth=1.6, zorder=0)
            ax.scatter(time_ms, row, s=92, color=color, zorder=3)
            if i in (0, 7):
                ax.annotate(f"{time_ms:.1f}", (time_ms, row), xytext=(8, 0),
                            textcoords="offset points", va="center", color=INK,
                            fontsize=10 if len(set(times)) == 1 else 11)
        ax.axvline(9, color="#B79532", linestyle="--", linewidth=1.7)
        ax.axvline(10, color="#576575", linestyle="-", linewidth=1.5)
        ax.set_xlim(-0.05, 10.55)
        ax.set_ylim(0.35, 8.65)
        ax.set_yticks(range(1, 9), labels=[f"候选{i}" for i in range(8, 0, -1)])
        ax.set_title(title, loc="left", pad=10, color=INK, fontweight="medium")
        ax.spines[["top", "right", "left"]].set_visible(False)
        ax.spines["bottom"].set_color(GRID)
        ax.tick_params(axis="y", length=0)
        ax.grid(axis="x", color=GRID, linewidth=0.8)
        ax.text(9.0, 8.45, "9 ms 停止受理", rotation=90,
                ha="right", va="top", color="#826D2D", fontsize=11)
    axes[-1].set_xticks(range(0, 11, 1))
    axes[-1].set_xlabel("控制窗内时间 / ms（点表示请求启动，不等于实际 PHY 发射）", color=INK)
    fig.tight_layout(rect=(0, 0, 1, 0.96), h_pad=2.0)
    save(fig, "01-release-timeline-cn")


def load_rows(filename: str) -> dict[str, dict[str, str]]:
    path = ROOT / "results" / filename / "capacity-pilot.csv"
    with path.open(newline="", encoding="utf-8") as stream:
        return {row["density"]: row for row in csv.DictReader(stream)
                if row["contenders"] == "8" and row["run"] == "81"}


def comparison() -> None:
    simultaneous = load_rows("density-static-pilot")
    staggered = load_rows("density-staggered-static-pilot")
    items = [("中等密度 · 一跳", "medium", "final_one_hop_f1"),
             ("中等密度 · 二跳", "medium", "final_two_hop_f1"),
             ("密集拓扑 · 一跳", "dense", "final_one_hop_f1"),
             ("密集拓扑 · 二跳", "dense", "final_two_hop_f1")]
    fig, ax = plt.subplots(figsize=(12.0, 5.7))
    fig.suptitle("只改变 Beacon 释放时刻：邻居表正确率的差别", color=INK, y=0.99)
    for i, (label, density, metric) in enumerate(items):
        y = 3 - i
        original = float(simultaneous[density][metric])
        revised = float(staggered[density][metric])
        ax.hlines(y, original, revised, color="#C7D2DA", linewidth=2.0, zorder=1)
        ax.scatter(original, y, s=150, marker="X", color=BLUE, zorder=3)
        ax.scatter(revised, y, s=140, marker="o", color=ORANGE, zorder=3)
        ax.annotate(f"{original:.3f}", (original, y), xytext=(-10, 12),
                    textcoords="offset points", ha="right", color=INK, fontsize=12)
        ax.annotate(f"{revised:.3f}", (revised, y), xytext=(-8, 12),
                    textcoords="offset points", ha="right", color=INK, fontsize=12)
    ax.set_yticks(range(3, -1, -1), [item[0] for item in items])
    ax.set_xlim(0.35, 1.08)
    ax.set_ylim(-0.6, 3.6)
    ax.set_xticks([0.4, 0.6, 0.8, 1.0])
    ax.set_xlabel("最终邻居集合 F1（1 表示无误报、无遗漏）", color=INK)
    ax.grid(axis="x", color=GRID, linewidth=0.9)
    ax.spines[["top", "right", "left"]].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="y", length=0)
    ax.scatter([], [], marker="X", s=125, color=BLUE, label="同时释放 · 原 V7")
    ax.scatter([], [], marker="o", s=125, color=ORANGE, label="本地错开 · V4 式")
    ax.legend(loc="lower left", bbox_to_anchor=(0.02, 1.01), ncol=2,
              frameon=False, fontsize=12)
    fig.text(0.52, 0.015,
             "同一条件：64 台、35 s、静态拓扑、K=8、标准地址、种子 81；这是诊断样本，不是五种子结论。",
             ha="center", color="#596878", fontsize=11)
    fig.tight_layout(rect=(0, 0.05, 1, 0.95))
    save(fig, "02-topology-f1-comparison-cn")


if __name__ == "__main__":
    timeline()
    comparison()
    print(OUT)
