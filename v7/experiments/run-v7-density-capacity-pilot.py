#!/usr/bin/env python3
"""Check whether one common Beacon contender count works at higher density."""

from __future__ import annotations

import argparse
import csv
import subprocess
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", default="81")
    parser.add_argument("--contenders", default="4,6")
    parser.add_argument("--densities", default="medium,dense")
    parser.add_argument("--duration", type=float, default=35.0)
    parser.add_argument("--topologyScenario", choices=("scripted", "static"), default="scripted")
    parser.add_argument("--controlReleaseMode", choices=("simultaneous", "staggered", "jitter"),
                        default="simultaneous")
    parser.add_argument("--controlReleaseSpreadUs", type=int, default=0)
    parser.add_argument("--output", default="self-organizing-mac-v7/results/density-capacity-pilot")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    binary = root / "build/scratch/self-organizing-mac-v7/ns3.47-self-organizing-64node-v7-default"
    output = root / args.output
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for density in args.densities.split(","):
        for contenders in (int(value) for value in args.contenders.split(",")):
            for run in (int(value) for value in args.runs.split(",")):
                case = output / f"{density}-k{contenders}-run{run}"
                case.mkdir(parents=True, exist_ok=True)
                if not (case / "summary.csv").is_file():
                    command = [str(binary), f"--duration={args.duration}", f"--run={run}",
                               "--controlFrame=beacon", "--beaconAddress=standard",
                               "--dataAddress=standard", f"--controlContenders={contenders}",
                               f"--controlReleaseMode={args.controlReleaseMode}",
                               f"--controlReleaseSpreadUs={args.controlReleaseSpreadUs}",
                               "--neighborEncoding=adaptive", f"--density={density}",
                               f"--topologyScenario={args.topologyScenario}", "--dataQueuePolicy=defer",
                               "--trafficPattern=burst", "--perNodeMbps=0.25",
                               "--amsduBytes=0", "--packetBytes=0", f"--output={case}"]
                    print(f"[RUN] {density} K={contenders} run={run}", flush=True)
                    with (case / "run.log").open("w", encoding="utf-8") as log:
                        subprocess.run(command, cwd=root, stdout=log,
                                       stderr=subprocess.STDOUT, check=True)
                with (case / "summary.csv").open(newline="", encoding="utf-8") as stream:
                    summary = next(csv.DictReader(stream))
                rows.append({"density": density, "contenders": contenders, "run": run,
                             **{key: summary[key] for key in (
                                 "control_tx", "control_rx", "final_one_hop_f1",
                                 "final_two_hop_f1", "final_exact_both_ratio",
                                 "control_window_attempt_occupancy",
                                 "topology_tx_crossed_window_boundary",
                                 "offered_completion_ratio")}})
                with (output / "capacity-pilot.csv").open("w", newline="", encoding="utf-8") as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
                    writer.writeheader()
                    writer.writerows(rows)
    print(f"[DONE] {len(rows)} cases in {output}", flush=True)


if __name__ == "__main__":
    main()
