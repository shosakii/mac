#!/usr/bin/env python3
"""Build per-event/per-hop summaries without hiding affected-node detail."""

import argparse
import csv
import statistics
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--validation", default="results/validation-v51")
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    validation = project / args.validation
    for address in ("standard", "short"):
        folder = validation / address
        with (folder / "event-detection.csv").open(newline="", encoding="utf-8") as handle:
            detail = list(csv.DictReader(handle))
        with (folder / "topology-events-ground-truth.csv").open(
                newline="", encoding="utf-8") as handle:
            events = list(csv.DictReader(handle))
        output = []
        for event in events:
            for hop in ("1", "2"):
                selected = [row for row in detail
                            if row["event_id"] == event["event_id"] and row["hop"] == hop]
                first = [float(row["first_correct_delay_s"]) for row in selected
                         if float(row["first_correct_delay_s"]) >= 0]
                stable = [float(row["stable_correct_delay_s"]) for row in selected
                          if float(row["stable_correct_delay_s"]) >= 0]
                output.append({
                    "event_id": event["event_id"], "event_type": event["event_type"],
                    "event_time_s": event["time_s"], "changed_node": event["node"],
                    "hop": hop, "affected_observers": len(selected),
                    "affected_observer_ids": ";".join(row["observer"] for row in selected),
                    "changed_entries": sum(int(row["changed_entries"]) for row in selected),
                    "first_correct_min_s": min(first) if first else -1,
                    "first_correct_mean_s": statistics.mean(first) if first else -1,
                    "first_correct_max_s": max(first) if first else -1,
                    "stable_correct_observers": len(stable),
                    "stable_correct_mean_s": statistics.mean(stable) if stable else -1,
                    "stable_correct_max_s": max(stable) if stable else -1,
                    "censored_observers": len(selected) - len(stable),
                })
        with (folder / "event-summary.csv").open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(output[0]))
            writer.writeheader()
            writer.writerows(output)
        print(f"[DONE] {folder / 'event-summary.csv'}")


if __name__ == "__main__":
    main()
