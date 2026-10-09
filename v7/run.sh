#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ns3_dir="$(cd "${project_dir}/.." && pwd)"
binary="${ns3_dir}/build/scratch/self-organizing-mac-v7/ns3.47-self-organizing-64node-v7-default"

cd "${ns3_dir}"
./ns3 build self-organizing-64node-v7
exec "${binary}" "$@"
