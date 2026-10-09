#!/usr/bin/env python3
"""Explicit lower-bound accounting for the experimental 16-bit address scheme.

This is deliberately separate from measured Beacon MPDU size.  It prevents the
report from claiming a 12-byte saving while silently ignoring address mapping
and admission management.  Join traffic is an analytical minimum because V5.1
starts with an already admitted network.
"""

import argparse
import csv
import math
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", default="64,128,256")
    parser.add_argument("--output",
                        default="self-organizing-mac-v6/results/short-address-cost.csv")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / args.output
    output.parent.mkdir(parents=True, exist_ok=True)

    # Minimal model: one request carries the permanent 48-bit identity; one
    # assignment carries that identity plus its 16-bit short address.  Each is
    # carried in a standard 24-byte management header plus 4-byte FCS.  ACK,
    # retry, authentication, security and PHY preamble costs are excluded, so
    # these values are a lower bound and are labelled as such in the CSV.
    request_mpdu = 24 + 6 + 4
    assignment_mpdu = 24 + 6 + 2 + 4
    per_join_air_bytes = request_mpdu + assignment_mpdu
    saved_per_beacon = 3 * (6 - 2)

    result = []
    for nodes in [int(value) for value in args.nodes.split(",")]:
        raw_table_per_radio = nodes * (6 + 2)
        raw_table_network = nodes * raw_table_per_radio
        initial_join_air_bytes = nodes * per_join_air_bytes
        result.append({
            "node_count": nodes,
            "raw_mapping_bytes_per_radio": raw_table_per_radio,
            "raw_mapping_bytes_network_total": raw_table_network,
            "minimum_join_management_mpdu_bytes_per_node": per_join_air_bytes,
            "minimum_initial_join_management_mpdu_bytes": initial_join_air_bytes,
            "beacon_bytes_saved_each": saved_per_beacon,
            "minimum_beacons_to_repay_join_air_bytes": math.ceil(
                initial_join_air_bytes / saved_per_beacon),
            "accounting_scope": "lower-bound:no-ACK,no-retry,no-security,no-PHY-preamble",
        })
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(result[0]))
        writer.writeheader()
        writer.writerows(result)
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
