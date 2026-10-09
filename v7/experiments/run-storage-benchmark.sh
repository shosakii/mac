#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "${project_dir}/results/storage-v51"
g++ -O2 -std=c++20 "${project_dir}/experiments/neighbor-storage-benchmark.cc" \
  -o "${project_dir}/results/storage-v51/neighbor-storage-benchmark"
"${project_dir}/results/storage-v51/neighbor-storage-benchmark" \
  > "${project_dir}/results/storage-v51/storage-benchmark.csv"
echo "[DONE] ${project_dir}/results/storage-v51/storage-benchmark.csv"
