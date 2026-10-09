#!/usr/bin/env python3
"""Generate report-ready V5.1 figures from CSV results."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def rows(path: Path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def f(row, key):
    return float(row[key])


def capacity_figure(result_root: Path, figure_root: Path):
    data = rows(result_root / "capacity-v51/capacity-aggregate.csv")
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.2))
    for address, marker in (("standard", "o"), ("short", "s")):
        selected = sorted((r for r in data if r["beacon_address"] == address),
                          key=lambda r: int(r["control_contenders"]))
        x = [int(r["control_contenders"]) for r in selected]
        axes[0].errorbar(x, [f(r, "mean_receiver_delivery") for r in selected],
                         yerr=[f(r, "receiver_delivery_ci95") for r in selected],
                         marker=marker, capsize=3, label=address)
        axes[1].plot(x, [f(r, "mean_final_one_hop_f1") for r in selected],
                     marker=marker, label=f"{address}: one-hop")
        axes[1].plot(x, [f(r, "mean_final_two_hop_f1") for r in selected],
                     marker=marker, linestyle="--", label=f"{address}: two-hop")
    axes[0].axhline(0.99, color="gray", linestyle=":", label="0.99 criterion")
    axes[0].set(xlabel="Contenders per control window (K)", ylabel="Receiver delivery ratio",
                ylim=(0, 1.03), title="Control-window receiver capacity")
    axes[1].set(xlabel="Contenders per control window (K)", ylabel="Final F1",
                ylim=(0, 1.03), title="Topology correctness after learning")
    for axis in axes:
        axis.grid(alpha=0.25)
        axis.legend(fontsize=8)
    fig.tight_layout()
    fig.savefig(figure_root / "capacity-and-f1.png", dpi=180)
    plt.close(fig)


def timeline_figure(validation: Path, figure_root: Path):
    data = rows(validation / "standard/topology-timeseries.csv")
    events = rows(validation / "standard/topology-events-ground-truth.csv")
    t = [f(r, "time_s") for r in data]
    fig, axes = plt.subplots(2, 1, figsize=(11, 6.4), sharex=True)
    axes[0].plot(t, [f(r, "one_hop_f1") for r in data], label="one-hop F1")
    axes[0].plot(t, [f(r, "two_hop_f1") for r in data], label="two-hop F1")
    axes[0].set(ylabel="F1", ylim=(-0.02, 1.03), title="Topology accuracy over node lifetime")
    axes[1].plot(t, [f(r, "one_hop_error_entries") for r in data], label="one-hop errors")
    axes[1].plot(t, [f(r, "two_hop_error_entries") for r in data], label="two-hop errors")
    axes[1].set(xlabel="Experiment time (s)", ylabel="Incorrect relationships",
                title="Incorrect relationships after topology events")
    for axis in axes:
        for event in events:
            axis.axvline(f(event, "time_s"), color="gray", linewidth=0.8, linestyle=":")
        axis.grid(alpha=0.25)
        axis.legend()
    fig.tight_layout()
    fig.savefig(figure_root / "topology-lifetime-timeline.png", dpi=180)
    plt.close(fig)


def lifetime_figure(validation: Path, figure_root: Path):
    fig, axis = plt.subplots(figsize=(11, 4.4))
    for address, marker in (("standard", "o"), ("short", "s")):
        data = rows(validation / address / "node-lifetime-summary.csv")
        values = sorted(f(r, "post_cold_exact_both_ratio") for r in data)
        axis.plot(range(len(values)), values, marker=marker, markersize=3, label=address)
    axis.set(xlabel="Nodes sorted by correctness ratio",
             ylabel="Post-cold-start exact one+two-hop ratio",
             ylim=(-0.02, 1.03), title="Correctness distribution across all 64 radios")
    axis.grid(alpha=0.25)
    axis.legend()
    fig.tight_layout()
    fig.savefig(figure_root / "node-lifetime-distribution.png", dpi=180)
    plt.close(fig)


def node_biography_figure(validation: Path, figure_root: Path):
    data = rows(validation / "standard/node-27-biography.csv")
    t = [f(r, "time_s") for r in data]
    one = [int(r["one_hop_fp"]) + int(r["one_hop_fn"]) for r in data]
    two = [int(r["two_hop_fp"]) + int(r["two_hop_fn"]) for r in data]
    fig, axis = plt.subplots(figsize=(11, 4.2))
    axis.step(t, one, where="post", label="node 27 one-hop errors")
    axis.step(t, two, where="post", label="node 27 two-hop errors")
    axis.set(xlabel="Experiment time (s)", ylabel="Incorrect relationships",
             title="Node 27 biography")
    axis.grid(alpha=0.25)
    axis.legend()
    fig.tight_layout()
    fig.savefig(figure_root / "node-27-biography.png", dpi=180)
    plt.close(fig)


def event_delay_figure(validation: Path, figure_root: Path):
    data = rows(validation / "standard/event-detection.csv")
    events = rows(validation / "standard/topology-events-ground-truth.csv")
    event_names = {
        r["event_id"]: f"E{r['event_id']} {r['event_type']}\nnode {r['node']} @ {float(r['time_s']):.2f}s"
        for r in events
    }
    event_ids = [r["event_id"] for r in events]
    fig, axes = plt.subplots(2, 1, figsize=(12, 7.2), sharex=True)
    for axis, metric, title in (
        (axes[0], "first_correct_delay_s", "First-correct delay of every affected observer"),
        (axes[1], "stable_correct_delay_s", "Stable-correct delay of every affected observer"),
    ):
        for hop, marker, color in (("1", "o", "tab:blue"), ("2", "x", "tab:orange")):
            for index, event_id in enumerate(event_ids):
                selected = [f(r, metric) for r in data if r["event_id"] == event_id and r["hop"] == hop]
                valid = [value for value in selected if value >= 0]
                if valid:
                    offset = -0.12 if hop == "1" else 0.12
                    axis.scatter([index + offset] * len(valid), valid, s=22, marker=marker,
                                 color=color, alpha=0.75,
                                 label=f"{hop}-hop observers" if index == 0 else None)
                censored = sum(value < 0 for value in selected)
                if censored:
                    axis.annotate(f"{censored} censored", (index, axis.get_ylim()[1]),
                                  xytext=(0, -18), textcoords="offset points",
                                  ha="center", va="top", fontsize=8, color="crimson")
        axis.set(ylabel="Delay after event (s)", title=title)
        axis.grid(alpha=0.25)
        axis.legend(loc="upper left")
    axes[1].set_xticks(range(len(event_ids)), [event_names[i] for i in event_ids], fontsize=8)
    axes[1].set_xlabel("Topology event")
    fig.tight_layout()
    fig.savefig(figure_root / "event-observer-delays.png", dpi=180)
    plt.close(fig)


def frame_size_figure(validation: Path, figure_root: Path):
    fig, axes = plt.subplots(2, 2, figsize=(11, 7.5), sharex=True)
    axes = axes.ravel()
    colors = {"standard": "tab:blue", "short": "tab:orange"}
    for address in ("standard", "short"):
        data = [r for r in rows(validation / address / "frame-size-distribution.csv")
                if r["frame_class"] == "TOPOLOGY_BEACON"]
        x = [int(r["serialized_mpdu_bytes"]) for r in data]
        y = [int(r["observations"]) for r in data]
        axes[0].plot(x, y, marker="o", label=address, color=colors[address])
        summary = rows(validation / address / "summary.csv")[0]
        axes[1].bar(address, f(summary, "topology_airtime_us") / 1000,
                    color=colors[address])
    axes[0].set(xlabel="Actually serialized Beacon MPDU (bytes)",
                ylabel="Observed PHY transmissions",
                title="Frame-size evidence from PhyTxPsduBegin")
    axes[1].set(ylabel="Beacon attempted airtime (ms)",
                title="35-second dynamic run")
    for axis in axes:
        axis.grid(axis="y", alpha=0.25)
    axes[0].legend()
    axes[0].annotate("12-byte reduction at every payload size",
                     xy=(118, 1474), xytext=(111, 1050),
                     arrowprops={"arrowstyle": "->", "color": "gray"}, fontsize=8)
    fig.tight_layout()
    fig.savefig(figure_root / "actual-frame-size-and-airtime.png", dpi=180)
    plt.close(fig)


def storage_figure(result_root: Path, figure_root: Path):
    data = rows(result_root / "storage-v51/storage-benchmark.csv")
    fig, axes = plt.subplots(2, 3, figsize=(13, 7.2), sharex="col")
    backends = ("map-set", "array-bitmap", "sparse-flat-list")
    styles = {"map-set": ("o", "tab:red"), "array-bitmap": ("s", "tab:blue"),
              "sparse-flat-list": ("^", "tab:green")}
    for column, node_limit in enumerate((64, 128, 256)):
        selected_n = [r for r in data if int(r["node_limit"]) == node_limit]
        for backend in backends:
            selected = sorted((r for r in selected_n if r["backend"] == backend),
                              key=lambda r: int(r["mean_degree"]))
            marker, color = styles[backend]
            x = [int(r["mean_degree"]) for r in selected]
            axes[0, column].plot(x, [f(r, "allocated_bytes") for r in selected],
                                 marker=marker, color=color, label=backend)
            axes[1, column].plot(x, [f(r, "two_hop_rebuild_us") for r in selected],
                                 marker=marker, color=color, label=backend)
        axes[0, column].set_title(f"Maximum node count N={node_limit}")
        axes[0, column].set_yscale("log")
        axes[1, column].set_yscale("log")
        axes[1, column].set_xlabel("Mean one-hop degree")
        for row in (0, 1):
            axes[row, column].grid(alpha=0.25, which="both")
    axes[0, 0].set_ylabel("Allocated bytes (log scale)")
    axes[1, 0].set_ylabel("Two-hop rebuild time (us, log scale)")
    axes[0, 2].legend(fontsize=8)
    fig.suptitle("Neighbor-table implementation cost: prototype versus embedded candidates")
    fig.tight_layout()
    fig.savefig(figure_root / "neighbor-storage-comparison.png", dpi=180)
    plt.close(fig)


def window_isolation_figure(validation: Path, figure_root: Path):
    summaries = {address: rows(validation / address / "summary.csv")[0]
                 for address in ("standard", "short")}
    fig, axes = plt.subplots(2, 1, figsize=(11, 5.2), gridspec_kw={"height_ratios": [1.25, 1]})
    axis = axes[0]
    axis.barh([0], [10], left=[0], color="tab:blue", height=0.45, label="Control window")
    axis.barh([0], [0.2], left=[10], color="0.7", height=0.45, label="Guard")
    axis.barh([0], [84.8], left=[10.2], color="tab:green", height=0.45, label="Data window")
    axis.barh([0], [5], left=[95], color="tab:purple", height=0.45, label="Drain guard")
    axis.axvline(10.2, color="black", linestyle=":", linewidth=1)
    axis.text(5, 0, "Beacon", ha="center", va="center", color="white", fontsize=9)
    axis.text(52.6, 0, "QoS Data allowed", ha="center", va="center", color="white")
    axis.set(xlim=(0, 100), yticks=[], xlabel="Time within one 100-ms superframe (ms)",
             title="Hard transmission gate used in V5.1")
    axis.legend(ncol=4, loc="upper center", bbox_to_anchor=(0.5, -0.32), fontsize=8)
    categories = ["Data PHY attempts\nin control window",
                  "Data attempts in\n95-100 ms guard",
                  "Data transmissions crossing\n100-ms boundary",
                  "Hard-gate deferrals"]
    x = np.arange(len(categories))
    width = 0.35
    for index, address in enumerate(("standard", "short")):
        values = [f(summaries[address], "data_phy_attempts_in_control_window"),
                  f(summaries[address], "data_phy_attempts_in_end_guard"),
                  f(summaries[address], "data_tx_crossed_superframe_boundary"),
                  f(summaries[address], "data_hard_gate_deferrals")]
        axes[1].bar(x + (index - 0.5) * width, values, width, label=address)
    axes[1].set_xticks(x, categories)
    axes[1].set_ylabel("Count in 35-second run")
    axes[1].set_title("Observed isolation result")
    axes[1].grid(axis="y", alpha=0.25)
    axes[1].legend()
    fig.tight_layout()
    fig.savefig(figure_root / "control-data-window-isolation.png", dpi=180)
    plt.close(fig)


def encoding_figure(result_root: Path, figure_root: Path):
    path = result_root / "encoding-v51/encoding-aggregate.csv"
    if not path.exists():
        return
    data = rows(path)
    encodings = ("list", "bitmap", "adaptive")
    fig, axes = plt.subplots(1, 3, figsize=(13, 4.4))
    x = np.arange(len(encodings))
    width = 0.35
    for index, address in enumerate(("standard", "short")):
        selected = {r["neighbor_encoding"]: r for r in data if r["beacon_address"] == address}
        offset = (index - 0.5) * width
        axes[0].bar(x + offset,
                    [f(selected[e], "mean_topology_airtime_us") / 1000 for e in encodings],
                    width, label=address)
        axes[1].bar(x + offset,
                    [f(selected[e], "mean_final_two_hop_f1") for e in encodings],
                    width, label=address)
        axes[2].bar(x + offset,
                    [f(selected[e], "mean_offered_completion_ratio") for e in encodings],
                    width, label=address)
    titles = ("Beacon airtime", "Final two-hop F1", "Offered traffic completion")
    labels = ("Airtime in 35 s (ms)", "F1", "Ratio")
    for axis, title, label in zip(axes, titles, labels):
        axis.set_xticks(x, encodings)
        axis.set_title(title)
        axis.set_ylabel(label)
        axis.grid(axis="y", alpha=0.25)
    axes[1].set_ylim(0, 1.03)
    axes[2].set_ylim(0, 1.03)
    axes[0].legend()
    fig.suptitle("Paired Beacon encoding comparison at safe K=8 (three seeds)")
    fig.tight_layout()
    fig.savefig(figure_root / "encoding-comparison.png", dpi=180)
    plt.close(fig)

    fig, axes = plt.subplots(1, 3, figsize=(13, 4.4))
    for index, address in enumerate(("standard", "short")):
        selected = {r["neighbor_encoding"]: r for r in data if r["beacon_address"] == address}
        offset = (index - 0.5) * width
        axes[0].bar(x + offset,
                    [f(selected[e], "mean_mean_first_correct_2hop_s") for e in encodings],
                    width, label=address)
        axes[1].bar(x + offset,
                    [f(selected[e], "mean_mean_stable_correct_2hop_s") for e in encodings],
                    width, label=address)
        axes[2].bar(x + offset,
                    [f(selected[e], "mean_mean_post_cold_exact_both_ratio") for e in encodings],
                    width, label=address)
    titles = ("Affected-node first correctness", "Affected-node stable correctness",
              "Whole-life correctness after cold start")
    labels = ("Mean two-hop delay (s)", "Mean two-hop delay (s)", "Exact ratio")
    for axis, title, label in zip(axes, titles, labels):
        axis.set_xticks(x, encodings)
        axis.set_title(title)
        axis.set_ylabel(label)
        axis.grid(axis="y", alpha=0.25)
    axes[2].set_ylim(0.9, 1.0)
    axes[0].legend()
    fig.suptitle("Beacon encoding timeliness and node-lifetime correctness")
    fig.tight_layout()
    fig.savefig(figure_root / "encoding-timeliness.png", dpi=180)
    plt.close(fig)


def aging_figure(result_root: Path, figure_root: Path):
    path = result_root / "aging-v51/aging-aggregate.csv"
    if not path.exists():
        return
    data = rows(path)
    fig, axes = plt.subplots(2, 2, figsize=(11, 7.5), sharex=True)
    axes = axes.ravel()
    for misses, marker in ((1, "o"), (2, "s"), (3, "^")):
        selected = sorted((r for r in data if int(r["neighbor_remove_misses"]) == misses),
                          key=lambda r: f(r, "control_loss_rate"))
        x = [100 * f(r, "control_loss_rate") for r in selected]
        axes[0].plot(x, [f(r, "mean_mean_post_cold_exact_both_ratio") for r in selected],
                     marker=marker, label=f"remove after {misses} misses")
        axes[1].plot(x, [f(r, "mean_time_mean_one_hop_false_negative") for r in selected],
                     marker=marker, label=f"remove after {misses} misses")
        axes[2].plot(x, [f(r, "mean_mean_node_fail_1hop_stable_delay_s") for r in selected],
                     marker=marker, label=f"remove after {misses} misses")
        axes[3].plot(x, [f(r, "mean_final_two_hop_f1") for r in selected],
                     marker=marker, label=f"remove after {misses} misses")
    axes[0].set(title="Post-cold-start lifetime correctness", ylabel="Exact one+two-hop ratio",
                ylim=(0, 1.03))
    axes[1].set(title="False deletion / missing live neighbors",
                ylabel="Mean one-hop false-negative entries")
    axes[2].set(title="Real failure detection", ylabel="Mean stable delay (s)")
    axes[3].set(title="Final two-hop correctness", ylabel="Final F1", ylim=(0, 1.03))
    for axis in axes:
        axis.set_xlabel("Injected Beacon loss (%)")
        axis.grid(alpha=0.25)
        axis.legend(fontsize=8)
    fig.suptitle("Neighbor-aging threshold trade-off (three seeds)")
    fig.tight_layout()
    fig.savefig(figure_root / "neighbor-aging-tradeoff.png", dpi=180)
    plt.close(fig)


def short_address_cost_figure(result_root: Path, figure_root: Path):
    path = result_root / "short-address-cost.csv"
    if not path.exists():
        return
    data = rows(path)
    nodes = [int(r["node_count"]) for r in data]
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.4))
    axes[0].bar([str(n) for n in nodes],
                [f(r, "raw_mapping_bytes_per_radio") for r in data], color="tab:purple")
    axes[0].set(xlabel="Network node limit", ylabel="Bytes per radio",
                title="Raw 48-bit identity to 16-bit ID mapping")
    axes[1].bar([str(n) for n in nodes],
                [f(r, "minimum_initial_join_management_mpdu_bytes") / 1024 for r in data],
                color="tab:brown")
    axes[1].set(xlabel="Initially admitted radios", ylabel="Minimum MPDU KiB",
                title="One-time join-management lower bound")
    for axis in axes:
        axis.grid(axis="y", alpha=0.25)
    fig.suptitle("Costs that must accompany the 12-byte short-Beacon saving")
    fig.tight_layout()
    fig.savefig(figure_root / "short-address-mapping-cost.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", default="results")
    parser.add_argument("--validation", default="validation-v51")
    parser.add_argument("--figures", default="results/figures-v51")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    result_root = project / args.results
    figure_root = project / args.figures
    figure_root.mkdir(parents=True, exist_ok=True)
    capacity_figure(result_root, figure_root)
    validation = result_root / args.validation
    timeline_figure(validation, figure_root)
    lifetime_figure(validation, figure_root)
    node_biography_figure(validation, figure_root)
    event_delay_figure(validation, figure_root)
    frame_size_figure(validation, figure_root)
    storage_figure(result_root, figure_root)
    window_isolation_figure(validation, figure_root)
    encoding_figure(result_root, figure_root)
    aging_figure(result_root, figure_root)
    short_address_cost_figure(result_root, figure_root)
    print(f"[DONE] {figure_root}")


if __name__ == "__main__":
    main()
