#!/usr/bin/env python3
"""Paired V7 address tests by topology density, packet length, or control loss.

Keep each factor separate.  Within every profile and seed, all four address
formats receive identical offered demands, checked from data-outcomes.csv.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import statistics
import subprocess
from pathlib import Path


MODES = {
    "A": ("standard", "standard"),
    "B": ("short", "standard"),
    "C": ("standard", "short"),
    "D": ("short", "short"),
}
DEMAND_FIELDS = ("offered_time_s", "source", "destination", "packet_bytes", "kind")
ZERO_FIELDS = (
    "topology_beacon_dequeues_outside_window",
    "topology_tx_started_outside_window",
    "topology_tx_crossed_window_boundary",
    "data_mac_dequeues_in_control_window",
    "data_phy_attempts_in_control_window",
    "data_phy_airtime_in_control_us",
    "data_phy_attempts_in_end_guard",
    "data_tx_crossed_superframe_boundary",
)
METRICS = (
    "offered_packets", "mac_accepted_packets", "rx_packets",
    "offered_completion_ratio", "mac_pdr", "topology_airtime_us",
    "control_window_attempt_occupancy", "data_airtime_us",
    "airtime_us_per_delivered_kib", "final_one_hop_f1", "final_two_hop_f1",
    "final_exact_both_ratio", "mean_queue_delay_ms", "p95_queue_delay_ms",
    "injected_control_drops", "control_rx", "topology_tx_bytes",
)


def one_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError(f"Expected one row in {path}")
    return rows[0]


def demand_digest(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    count = 0
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            digest.update("|".join(row[field] for field in DEMAND_FIELDS).encode())
            digest.update(b"\n")
            count += 1
    return count, digest.hexdigest()


def write_rows(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def profiles(args: argparse.Namespace) -> list[tuple[str, str, int, float]]:
    if args.study == "density":
        return [(density, density, 0, 0.0) for density in args.densities.split(",")]
    if args.study == "packet":
        return [(f"packet{size}", args.density, int(size), 0.0)
                for size in args.packetBytes.split(",")]
    return [(f"loss{int(float(loss) * 100):02d}", args.density, 0, float(loss))
            for loss in args.controlLossRates.split(",")]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--study", choices=("density", "packet", "loss"), required=True)
    parser.add_argument("--runs", default="81,82,83,84,85")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--densities", default="sparse,medium,dense")
    parser.add_argument("--density", default="medium")
    parser.add_argument("--packetBytes", default="256,1024,1472")
    parser.add_argument("--controlLossRates", default="0,0.05,0.10")
    parser.add_argument("--perNodeMbps", type=float, default=0.25)
    parser.add_argument("--amsduBytes", type=int, default=0)
    parser.add_argument("--controlReleaseMode", choices=("simultaneous", "staggered", "jitter"),
                        default="simultaneous")
    parser.add_argument("--controlReleaseSpreadUs", type=int, default=0)
    parser.add_argument("--output", default="")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = root / "build/scratch/self-organizing-mac-v7/ns3.47-self-organizing-64node-v7-default"
    output = root / (args.output or f"self-organizing-mac-v7/results/robustness-{args.study}")
    output.mkdir(parents=True, exist_ok=True)
    if not binary.is_file() and not args.validate_only:
        raise FileNotFoundError(binary)
    rows: list[dict[str, object]] = []
    pairs: list[dict[str, object]] = []

    for profile, density, packet_bytes, loss_rate in profiles(args):
        if density not in ("sparse", "medium", "dense"):
            raise ValueError(f"Unsupported density: {density}")
        if packet_bytes not in (0, 256, 1024, 1472):
            raise ValueError(f"Unsupported packet size: {packet_bytes}")
        if not 0 <= loss_rate <= 1:
            raise ValueError(f"Invalid loss rate: {loss_rate}")
        for run in (int(value) for value in args.runs.split(",")):
            digests: dict[str, tuple[int, str]] = {}
            for mode, (beacon, data) in MODES.items():
                case = output / f"{profile}-run{run:02d}-{mode}"
                case.mkdir(parents=True, exist_ok=True)
                required = ("summary.csv", "run-config.csv", "data-outcomes.csv")
                complete = all((case / name).is_file() and (case / name).stat().st_size > 0
                               for name in required)
                if not args.validate_only and not complete:
                    command = [
                        str(binary), f"--duration={args.duration}", f"--run={run}",
                        "--controlFrame=beacon", f"--beaconAddress={beacon}",
                        f"--dataAddress={data}", "--controlContenders=8",
                        f"--controlReleaseMode={args.controlReleaseMode}",
                        f"--controlReleaseSpreadUs={args.controlReleaseSpreadUs}",
                        "--neighborEncoding=adaptive", f"--density={density}",
                        "--topologyScenario=scripted", "--dataQueuePolicy=defer",
                        "--trafficPattern=burst", f"--perNodeMbps={args.perNodeMbps}",
                        f"--amsduBytes={args.amsduBytes}", f"--packetBytes={packet_bytes}",
                        f"--controlLossRate={loss_rate}", f"--output={case}",
                    ]
                    print(f"[RUN] {profile} seed={run} mode={mode}", flush=True)
                    with (case / "run.log").open("w", encoding="utf-8") as log:
                        subprocess.run(command, cwd=root, stdout=log,
                                       stderr=subprocess.STDOUT, check=True)
                summary = one_row(case / "summary.csv")
                config = one_row(case / "run-config.csv")
                if float(config["duration_s"]) != args.duration or \
                        int(config["fixed_packet_bytes"]) != packet_bytes or \
                        float(config["control_loss_rate"]) != loss_rate or \
                        float(config["per_node_mbps"]) != args.perNodeMbps or \
                        int(config["amsdu_bytes"]) != args.amsduBytes or \
                        config["control_release_mode"] != args.controlReleaseMode or \
                        int(config["control_release_spread_us"]) != args.controlReleaseSpreadUs or \
                        int(config["run"]) != run or \
                        config["beacon_address"] != beacon or \
                        config["data_address"] != data or \
                        config["density"] != density:
                    raise ValueError(f"Result config differs from request: {case}")
                violations = {field: summary[field] for field in ZERO_FIELDS
                              if float(summary[field]) != 0}
                if violations:
                    raise ValueError(f"Window violation in {case}: {violations}")
                digests[mode] = demand_digest(case / "data-outcomes.csv")
                with (case / "node-lifetime-summary.csv").open(
                        newline="", encoding="utf-8") as stream:
                    lifetimes = list(csv.DictReader(stream))
                if len(lifetimes) != 64:
                    raise ValueError(f"Expected 64 node lifetimes in {case}")
                lifetime_ratios = [float(node["post_cold_exact_both_ratio"])
                                   for node in lifetimes]
                rows.append({
                    "study": args.study, "profile": profile, "density": density,
                    "packet_bytes": packet_bytes, "control_loss_rate": loss_rate,
                    "duration_s": args.duration, "per_node_mbps": args.perNodeMbps,
                    "amsdu_bytes": args.amsduBytes, "run": run, "mode": mode,
                    "control_release_mode": args.controlReleaseMode,
                    "control_release_spread_us": args.controlReleaseSpreadUs,
                    "beacon_address": beacon, "data_address": data,
                    **{key: summary[key] for key in METRICS},
                    "mean_node_lifetime_exact_ratio": statistics.mean(lifetime_ratios),
                    "worst_node_lifetime_exact_ratio": min(lifetime_ratios),
                })
                write_rows(output / "per-run.csv", rows)
            if len(set(digests.values())) != 1:
                raise ValueError(f"Offered demands differ: {profile} seed={run}: {digests}")
            pairs.append({"profile": profile, "run": run,
                          "offered_demands": digests["A"][0],
                          "offered_sha256": digests["A"][1],
                          "identical_offered_demands": 1})
            write_rows(output / "pairing-checks.csv", pairs)
            print(f"[PASS] {profile} seed={run} paired demands={digests['A'][0]}", flush=True)
    print(f"[DONE] {len(rows)} cases, {len(pairs)} paired profiles in {output}", flush=True)


if __name__ == "__main__":
    main()
