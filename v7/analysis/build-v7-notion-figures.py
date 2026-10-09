#!/usr/bin/env python3
"""Build Chinese, source-grounded static figures for the V7 Notion report."""

from __future__ import annotations

import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle


BASE = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent / "figures"
OUT.mkdir(parents=True, exist_ok=True)
BLUE = "#165D8C"
TEAL = "#168A80"
ORANGE = "#D9822B"
PURPLE = "#7A62A5"
GRAY = "#667085"
GRID = "#DDE3E8"
MODES = {"A": "双标准", "B": "仅信标短地址", "C": "仅数据短地址", "D": "两者短地址"}

plt.rcParams.update({
    "font.family": "Microsoft YaHei",
    "axes.unicode_minus": False,
    "svg.fonttype": "none",
    "figure.facecolor": "white",
    "axes.facecolor": "white",
    "axes.edgecolor": GRID,
    "axes.labelcolor": "#243247",
    "xtick.color": GRAY,
    "ytick.color": GRAY,
    "text.color": "#243247",
    "font.size": 11,
    "axes.titlesize": 14,
})


def rows(name: str) -> list[dict[str, str]]:
    with (BASE / "results" / name / "matrix-summary.csv").open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


NO_AGG = rows("address-matrix-no-amsdu")
AGG = rows("address-matrix-amsdu2304")


def indexed(data: list[dict[str, str]]) -> dict[tuple[int, str], dict[str, str]]:
    return {(int(row["run"]), row["mode"]): row for row in data}


def save(fig: plt.Figure, stem: str) -> None:
    fig.savefig(OUT / f"{stem}.svg", bbox_inches="tight")
    fig.savefig(OUT / f"{stem}.png", dpi=190, bbox_inches="tight")
    plt.close(fig)


def frame_structure() -> None:
    fig, axs = plt.subplots(2, 1, figsize=(12.5, 6.7), gridspec_kw={"hspace": 0.52})

    def one(ax, title, specs):
        ax.set_title(title, loc="left", fontweight="bold")
        ax.set_xlim(0, 30)
        ax.set_ylim(-0.4, 2.25)
        ax.axis("off")
        for y, label, fields in [(1.25, "标准地址", specs[0]), (0.20, "短地址", specs[1])]:
            ax.text(-0.3, y + 0.37, label, ha="right", va="center", fontsize=11)
            x = 0.0
            for name, width, color in fields:
                ax.add_patch(Rectangle((x, y), width, 0.75, facecolor=color,
                                       edgecolor="white", linewidth=1.8))
                ax.text(x + width / 2, y + 0.38, f"{name}\n{width} B", ha="center",
                        va="center", color="white", fontsize=9 if width <= 2 else 10)
                x += width
            ax.text(x + 0.3, y + 0.37, f"首部共 {int(x)} B", va="center", fontweight="bold")

    common = [("帧控制", 2, GRAY), ("时长", 2, GRAY)]
    three6 = [("地址1", 6, BLUE), ("地址2", 6, BLUE), ("地址3", 6, BLUE)]
    three2 = [("地址1", 2, TEAL), ("地址2", 2, TEAL), ("地址3", 2, TEAL)]
    one(axs[0], "Beacon 管理帧：三个地址字段各缩短 4 B",
        [common + three6 + [("序列", 2, PURPLE)],
         common + three2 + [("序列", 2, PURPLE)]])
    one(axs[1], "三地址 Ad hoc QoS Data：三个地址字段各缩短 4 B",
        [common + three6 + [("序列", 2, PURPLE), ("QoS", 2, PURPLE)],
         common + three2 + [("序列", 2, PURPLE), ("QoS", 2, PURPLE)]])
    fig.text(0.02, 0.02, "注：FCS 仍为 4 B；ACK/RTS/CTS、PHY 与业务载荷未改。短地址帧不兼容未修改的 802.11n 电台。",
             color=GRAY, fontsize=10)
    save(fig, "01-frame-structure")


def mpdu_size() -> None:
    a = BASE / "results/address-matrix-no-amsdu/run-01-A/frame-size-distribution.csv"
    c = BASE / "results/address-matrix-no-amsdu/run-01-C/frame-size-distribution.csv"
    def size_set(path):
        with path.open(newline="", encoding="utf-8") as f:
            return sorted(int(r["serialized_mpdu_bytes"]) for r in csv.DictReader(f)
                          if r["frame_class"] == "DATA")
    standard, short = size_set(a), size_set(c)
    assert standard == [294, 1062, 1510] and short == [282, 1050, 1498]
    packet = [256, 1024, 1472]
    fig, ax = plt.subplots(figsize=(10, 4.2))
    reduction = [100 * (x - y) / x for x, y in zip(standard, short)]
    bars = ax.barh(range(3), reduction, color=TEAL, height=0.46)
    ax.set_yticks(range(3), [f"业务包 {p} B" for p in packet])
    ax.invert_yaxis()
    ax.set_xlim(0, 5.3)
    ax.set_xlabel("单个 DATA MPDU 的总帧长减少比例 / %")
    ax.set_title("实测：三种包长都恰好减少 12 B", loc="left", fontweight="bold")
    ax.grid(axis="x", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for bar, x, y, pct in zip(bars, standard, short, reduction):
        ax.text(bar.get_width() + 0.12, bar.get_y() + bar.get_height() / 2,
                f"{x} → {y} B  （−{pct:.2f}%）", va="center", fontsize=11)
    save(fig, "02-mpdu-size")


def control_airtime() -> None:
    by = indexed(NO_AGG)
    means = [sum(float(by[s, m]["topology_airtime_us"]) for s in range(1, 6)) / 5000
             for m in "ABCD"]
    fig, ax = plt.subplots(figsize=(10, 4.4))
    colors = [BLUE, TEAL, BLUE, TEAL]
    bars = ax.bar(range(4), means, color=colors, width=0.62)
    ax.set_xticks(range(4), [f"{m}  {MODES[m]}" for m in "ABCD"])
    ax.set_ylabel("拓扑帧累计发送空口时间 / ms")
    ax.set_ylim(0, 285)
    ax.set_title("短 Beacon 的控制发送空口时间下降 8.10%", loc="left", fontweight="bold")
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    for bar, val in zip(bars, means):
        ax.text(bar.get_x() + bar.get_width() / 2, val + 5, f"{val:.1f}",
                ha="center", fontweight="bold")
    fig.subplots_adjust(bottom=0.23)
    fig.text(0.02, 0.01, "40 组中控制空口时间仅随 Beacon 地址模式变化；该值是各节点发送时长的累计，不等于信道占用率。",
             color=GRAY, fontsize=10)
    save(fig, "03-control-airtime")


def paired_metric(metric: str, stem: str, title: str, ylabel: str, ylim: tuple[float, float]) -> None:
    fig, axs = plt.subplots(1, 2, figsize=(12.6, 5), sharey=True)
    for ax, data, caption in zip(axs, (NO_AGG, AGG), ("禁用 A-MSDU", "A-MSDU 上限 2304 B")):
        by = indexed(data)
        for x, mode, color in zip(range(3), "BCD", (ORANGE, TEAL, PURPLE)):
            values = [100 * (float(by[s, mode][metric]) / float(by[s, "A"][metric]) - 1)
                      for s in range(1, 6)]
            offsets = [-0.12, -0.06, 0, 0.06, 0.12]
            ax.scatter([x + off for off in offsets], values, color=color, s=43,
                       alpha=0.72, zorder=3)
            mean = sum(values) / len(values)
            ax.hlines(mean, x - 0.24, x + 0.24, color=color, linewidth=3)
            ax.text(x, ylim[1] - 0.3, f"均值 {mean:+.2f}%", ha="center", color=color,
                    fontsize=10, fontweight="bold")
        ax.axhline(0, color=GRAY, linewidth=1)
        ax.set_xticks(range(3), ["只缩信标\nA→B", "只缩数据\nA→C", "两者都缩\nA→D"])
        ax.set_ylim(*ylim)
        ax.set_title(caption, fontweight="bold")
        ax.grid(axis="y", color=GRID, linewidth=0.8)
        ax.set_axisbelow(True)
    axs[0].set_ylabel(ylabel)
    fig.subplots_adjust(bottom=0.27, top=0.85, wspace=0.20)
    fig.suptitle(title, x=0.07, y=1.0, ha="left", fontweight="bold", fontsize=15)
    fig.text(0.03, 0.01, "每个圆点对应一个随机种子；短横线为五种子配对均值。正负方向只表示变化，不保证因果。",
             color=GRAY, fontsize=10)
    save(fig, stem)


def retry_variation() -> None:
    by = indexed(NO_AGG)
    fig, ax = plt.subplots(figsize=(9.6, 4.6))
    for seed in range(1, 6):
        standard = float(by[seed, "A"]["data_retry_mpdus"])
        short = float(by[seed, "C"]["data_retry_mpdus"])
        ax.plot([0, 1], [standard, short], color=GRID, linewidth=1.7, zorder=1)
        ax.scatter([0, 1], [standard, short], color=[BLUE, TEAL], s=52, zorder=2)
        ax.text(1.05, short, f"种子{seed}", va="center", fontsize=9, color=GRAY)
    ax.set_xlim(-0.25, 1.55)
    ax.set_xticks([0, 1], ["A 标准数据地址", "C 短数据地址"])
    ax.set_ylabel("DATA 重传 MPDU 次数")
    ax.set_title("单帧缩短≠重传必然减少", loc="left", fontweight="bold")
    ax.grid(axis="y", color=GRID, linewidth=0.8)
    ax.set_axisbelow(True)
    fig.subplots_adjust(bottom=0.23)
    fig.text(0.02, 0.01, "无聚合五种子均值：A 为 9,582 次，C 为 10,010.8 次；这是竞争过程的观测，不能单独据此建立因果。",
             color=GRAY, fontsize=10)
    save(fig, "06-retry-variation")


def topology_timeline() -> None:
    path = BASE / "results/address-matrix-no-amsdu/run-01-A/topology-timeseries.csv"
    with path.open(newline="", encoding="utf-8") as f:
        data = list(csv.DictReader(f))
    times = [float(r["time_s"]) for r in data]
    one = [float(r["one_hop_f1"]) for r in data]
    two = [float(r["two_hop_f1"]) for r in data]
    fig, ax = plt.subplots(figsize=(11.7, 4.5))
    ax.plot(times, one, color=BLUE, linewidth=2, label="全网一跳 F1")
    ax.plot(times, two, color=TEAL, linewidth=2, label="全网二跳 F1")
    events = [(4.05, "27失效"), (9.03, "27恢复"), (14.07, "36移动")]
    for x, label in events:
        ax.axvline(x, color=ORANGE, linestyle="--", linewidth=1)
        ax.text(x + 0.08, 0.42, label, rotation=90, fontsize=9, color=ORANGE, va="bottom")
    ax.set_xlim(0, 15)
    ax.set_ylim(0, 1.04)
    ax.set_xlabel("仿真时间 / s")
    ax.set_ylabel("与真实拓扑对照得到的 F1")
    ax.set_title("代表性运行：冷启动与前三次拓扑事件", loc="left", fontweight="bold")
    ax.legend(loc="lower right", frameon=False)
    ax.grid(color=GRID, linewidth=0.7)
    fig.subplots_adjust(bottom=0.24)
    fig.text(0.02, 0.01, "取无聚合 A 组、种子1；15 s 仅覆盖六次预设事件中的前三次，不能代替完整节点生命史。",
             color=GRAY, fontsize=10)
    save(fig, "07-topology-timeline")


def main() -> None:
    frame_structure()
    mpdu_size()
    control_airtime()
    paired_metric("airtime_us_per_delivered_kib", "04-data-efficiency",
                  "数据空口效率：缩短帧首部的收益存在种子波动",
                  "每交付 KiB 数据空口时间相对 A 变化 / %（负值较省）", (-3.5, 4.1))
    paired_metric("rx_packets", "05-delivery",
                  "成功接收包数：未观察到跨两种聚合条件的稳定单调提升",
                  "成功接收包数相对 A 变化 / %", (-3.0, 4.3))
    retry_variation()
    topology_timeline()
    print(f"Wrote 7 SVG and 7 PNG figures to {OUT}")


if __name__ == "__main__":
    main()
