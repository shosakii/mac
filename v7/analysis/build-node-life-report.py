#!/usr/bin/env python3
"""Select representative radios and build auditable whole-life artifacts.

The selection is rule based, not hand picked after looking at a pretty curve.
Each selected radio keeps its full 10-ms topology trace, state transitions,
event impacts and traffic outcomes so a reviewer can reconstruct its life.
"""

import argparse
import csv
import shutil
import statistics
from collections import Counter, defaultdict
from pathlib import Path


def read_rows(path: Path):
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def write_rows(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def choose_unique(candidates, selected):
    for node in candidates:
        if node not in selected:
            return node
    return candidates[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("run_dir", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    run_dir = args.run_dir.resolve()
    output = (args.output or run_dir / "node-life-report").resolve()
    output.mkdir(parents=True, exist_ok=True)

    lifetime = read_rows(run_dir / "node-lifetime-summary.csv")
    metrics = read_rows(run_dir / "node-topology-metrics.csv")
    transitions = read_rows(run_dir / "node-lifetime-transitions.csv")
    detection = read_rows(run_dir / "event-detection.csv")
    events = read_rows(run_dir / "topology-events-ground-truth.csv")
    outcomes = read_rows(run_dir / "data-outcomes.csv")

    by_node = {int(row["observer"]): row for row in lifetime}
    nodes = sorted(by_node)
    one_events = Counter()
    two_events = Counter()
    two_only_events = Counter()
    one_by_event = defaultdict(set)
    two_by_event = defaultdict(set)
    for row in detection:
        node = int(row["observer"])
        event_id = int(row["event_id"])
        if row["hop"] == "1":
            one_events[node] += 1
            one_by_event[event_id].add(node)
        else:
            two_events[node] += 1
            two_by_event[event_id].add(node)
    for event_id, affected in two_by_event.items():
        for node in affected - one_by_event[event_id]:
            two_only_events[node] += 1
    actor_events = Counter(int(row["node"]) for row in events)

    ratio = {node: float(by_node[node]["post_cold_exact_both_ratio"]) for node in nodes}
    max_error = {node: float(by_node[node]["max_continuous_error_s"]) for node in nodes}
    median_ratio = statistics.median(ratio.values())
    selected = []
    roles = []

    worst_order = sorted(nodes, key=lambda n: (ratio[n], -max_error[n], n))
    worst = choose_unique(worst_order, selected)
    selected.append(worst)
    roles.append(("worst_lifetime", worst,
                  "minimum post-cold-start exact one+two-hop lifetime ratio"))

    median_order = sorted(nodes, key=lambda n: (abs(ratio[n] - median_ratio), n))
    median = choose_unique(median_order, selected)
    selected.append(median)
    roles.append(("median_lifetime", median,
                  "closest to the median post-cold-start correctness ratio"))

    one_order = sorted(nodes, key=lambda n: (-one_events[n], ratio[n], n))
    direct = choose_unique(one_order, selected)
    selected.append(direct)
    roles.append(("most_one_hop_affected", direct,
                  "largest number of topology events changing its one-hop truth"))

    two_order = sorted(nodes, key=lambda n: (-two_only_events[n], ratio[n], n))
    two_only = choose_unique(two_order, selected)
    selected.append(two_only)
    roles.append(("most_two_hop_only_affected", two_only,
                  "largest number of events changing two-hop but not one-hop truth"))

    actor_order = sorted(nodes, key=lambda n: (-actor_events[n], ratio[n], n))
    actor = choose_unique(actor_order, selected)
    selected.append(actor)
    roles.append(("event_subject", actor,
                  "radio directly failed, recovered, or moved most often"))

    stable_order = sorted(nodes,
                          key=lambda n: (one_events[n] + two_events[n], -ratio[n], n))
    stable = choose_unique(stable_order, selected)
    selected.append(stable)
    roles.append(("stable_control", stable,
                  "least event exposure, then highest lifetime correctness"))

    traffic_by_source = defaultdict(list)
    for row in outcomes:
        traffic_by_source[int(row["source"])].append(row)

    overview = []
    role_by_node = defaultdict(list)
    for role, node, rationale in roles:
        role_by_node[node].append(role)
        traffic = traffic_by_source[node]
        decision_counts = Counter(row["decision"] for row in traffic)
        accepted = sum(int(row["mac_accepted"]) for row in traffic)
        received = sum(int(row["received"]) for row in traffic)
        row = by_node[node]
        overview.append({
            "role": role,
            "observer": node,
            "selection_rule": rationale,
            "cold_start_first_exact_both_s": row["cold_start_first_exact_both_s"],
            "post_cold_exact_both_ratio": row["post_cold_exact_both_ratio"],
            "error_intervals": row["error_intervals"],
            "max_continuous_error_s": row["max_continuous_error_s"],
            "one_hop_affected_events": one_events[node],
            "two_hop_affected_events": two_events[node],
            "two_hop_only_events": two_only_events[node],
            "event_subject_count": actor_events[node],
            "offered_packets": len(traffic),
            "mac_accepted_packets": accepted,
            "received_packets": received,
            "blocked_false_negative": decision_counts["BLOCKED_FALSE_NEGATIVE"],
            "sent_false_positive": decision_counts["SENT_FALSE_POSITIVE"],
            "source_dead": decision_counts["SOURCE_DEAD"],
        })
    write_rows(output / "representative-nodes.csv", overview)

    for node in selected:
        folder = output / f"node-{node:02d}"
        node_metrics = [row for row in metrics if int(row["observer"]) == node]
        node_transitions = [row for row in transitions if int(row["observer"]) == node]
        node_detection = [row for row in detection if int(row["observer"]) == node]
        node_outcomes = traffic_by_source[node]
        write_rows(folder / "topology-timeseries.csv", node_metrics)
        write_rows(folder / "state-transitions.csv", node_transitions)
        write_rows(folder / "event-detection.csv", node_detection)
        write_rows(folder / "traffic-outcomes.csv", node_outcomes)
        write_rows(folder / "summary.csv",
                   [row for row in overview if int(row["observer"]) == node])

    # Preserve the exact event schedule beside the selected lives.
    shutil.copy2(run_dir / "topology-events-ground-truth.csv",
                 output / "topology-events-ground-truth.csv")
    print(f"[DONE] {output}")


if __name__ == "__main__":
    main()
