#!/usr/bin/env python3
"""Run and validate a paired four-mode Beacon/QoS-Data address experiment.

The ns-3 streams are fixed per run.  A comparison is accepted only if the
actual offered-demand sequence (time, source, destination, bytes, kind) is
identical across all four modes.  This is an observable equivalence check,
not a claim that the simulator replays an external traffic file.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
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
REPORT_FIELDS = (
    "offered_packets", "mac_accepted_packets", "rx_packets",
    "offered_completion_ratio", "mac_pdr", "data_airtime_us",
    "airtime_us_per_delivered_kib", "topology_airtime_us",
    "final_one_hop_f1", "final_two_hop_f1", "mean_queue_delay_ms",
    "p95_queue_delay_ms", "data_amsdu_mpdus", "data_amsdu_subframes",
)


def read_one(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 1:
        raise ValueError(f"Expected one summary row: {path}")
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


def data_sizes(path: Path) -> set[int]:
    with path.open(newline="", encoding="utf-8") as stream:
        return {int(row["serialized_mpdu_bytes"]) for row in csv.DictReader(stream)
                if row["frame_class"] == "DATA"}


def data_airtime_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as stream:
        for row in csv.DictReader(stream):
            if row["frame_class"] == "DATA":
                return row
    raise ValueError(f"No DATA airtime row: {path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=float, default=15.0)
    parser.add_argument("--runs", default="1,2,3,4,5", help="Comma-separated ns-3 run numbers")
    parser.add_argument("--amsduBytes", type=int, default=0)
    parser.add_argument("--topologyScenario", choices=("static", "scripted"), default="scripted")
    parser.add_argument("--output", default="self-organizing-mac-v7/results/address-matrix-no-amsdu")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    ns3_root = Path(__file__).resolve().parents[2]
    binary = ns3_root / "build/scratch/self-organizing-mac-v7/ns3.47-self-organizing-64node-v7-default"
    output = ns3_root / args.output
    output.mkdir(parents=True, exist_ok=True)
    runs = [int(value) for value in args.runs.split(",")]
    all_rows: list[dict[str, str]] = []
    checks: list[dict[str, str]] = []

    for run in runs:
        digests: dict[str, tuple[int, str]] = {}
        size_sets: dict[str, set[int]] = {}
        for mode, (beacon, data) in MODES.items():
            result_dir = output / f"run-{run:02d}-{mode}"
            if not args.validate_only:
                result_dir.mkdir(parents=True, exist_ok=True)
                command = [
                    str(binary), f"--duration={args.duration}", f"--run={run}",
                    "--controlFrame=beacon", f"--beaconAddress={beacon}",
                    f"--dataAddress={data}", "--controlContenders=8",
                    "--neighborEncoding=adaptive", "--density=sparse",
                    f"--topologyScenario={args.topologyScenario}",
                    "--dataQueuePolicy=defer", "--trafficPattern=burst",
                    f"--amsduBytes={args.amsduBytes}",
                    f"--output={result_dir}",
                ]
                with (result_dir / "run.log").open("w", encoding="utf-8") as log:
                    subprocess.run(command, cwd=ns3_root, stdout=log,
                                   stderr=subprocess.STDOUT, check=True)
            summary = read_one(result_dir / "summary.csv")
            for field in ZERO_FIELDS:
                if float(summary[field]) != 0:
                    raise ValueError(f"Window violation: run {run} mode {mode} {field}={summary[field]}")
            digests[mode] = demand_digest(result_dir / "data-outcomes.csv")
            size_sets[mode] = data_sizes(result_dir / "frame-size-distribution.csv")
            data_airtime = data_airtime_row(result_dir / "airtime-summary.csv")
            if not size_sets[mode]:
                raise ValueError(f"No transmitted DATA MPDUs: run {run} mode {mode}")
            all_rows.append({"run": str(run), "mode": mode,
                             "beacon_address": beacon, "data_address": data,
                             **{key: summary[key] for key in REPORT_FIELDS},
                             **{f"data_{key}": data_airtime[key] for key in
                                ("ppdu_attempts", "mpdu_attempts", "retry_mpdus", "mpdu_bytes")}})
            print(f"run={run} mode={mode} rx={summary['rx_packets']} DATA sizes={sorted(size_sets[mode])[:8]}", flush=True)

        if len(set(digests.values())) != 1:
            raise ValueError(f"Offered demands differ across modes in run {run}: {digests}")
        if args.amsduBytes == 0:
            if {size - 12 for size in size_sets["A"]} != size_sets["C"]:
                raise ValueError(f"Data header size difference is not exactly 12 B in run {run}")
            if {size - 12 for size in size_sets["B"]} != size_sets["D"]:
                raise ValueError(f"Data header size difference is not exactly 12 B in run {run}")
        checks.append({"run": str(run), "offered_demands": str(digests["A"][0]),
                       "offered_sha256": digests["A"][1],
                       "identical_offered_demands": "1",
                       "data_header_delta_12B": "1" if args.amsduBytes == 0 else "not_checked_amsdu"})

    for filename, rows in (("matrix-summary.csv", all_rows), ("pairing-checks.csv", checks)):
        with (output / filename).open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    print(f"Validated {len(runs)} paired runs, {len(all_rows)} simulations: {output}")


if __name__ == "__main__":
    main()
