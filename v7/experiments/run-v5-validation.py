#!/usr/bin/env python3
"""Run the paired V5.1 dynamic validation after capacity calibration."""

from pathlib import Path
import subprocess


def main() -> None:
    root = Path(__file__).resolve().parents[2]
    binary = root / "build/scratch/self-organizing-mac-v6/ns3.47-self-organizing-64node-v6-default"
    for address in ("standard", "short"):
        output = Path("self-organizing-mac-v6/results/validation-v51") / address
        command = [
            str(binary),
            "--duration=35",
            "--controlFrame=beacon",
            f"--beaconAddress={address}",
            "--controlContenders=8",
            "--neighborEncoding=adaptive",
            "--density=sparse",
            "--topologyScenario=scripted",
            "--trafficPattern=burst",
            "--dataQueuePolicy=defer",
            "--perNodeMbps=0.25",
            "--run=41",
            "--pcap=1",
            f"--output={output}",
        ]
        print(f"[RUN] {address}", flush=True)
        subprocess.run(command, cwd=root, check=True)
    print("[DONE] paired dynamic validation", flush=True)


if __name__ == "__main__":
    main()
