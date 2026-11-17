#!/usr/bin/env bash
# Build one ChampSim binary per JSON config, each configured on its own.
# (config.sh merges multiple files into a single config, so they must be built one at a time.)
# Usage: scripts/build_configs.sh build_configs/a.json [build_configs/b.json ...]
# Extra compiler definitions can be passed via EXTRA_CXXFLAGS, e.g. EXTRA_CXXFLAGS=-DHP_TABLE_BITS=13
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .cache/logs
for cfg in "$@"; do
  name=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["executable_name"])' "$cfg")
  ./config.sh "$cfg" > ".cache/logs/config_${name}.log" 2>&1
  if ! make -j"$(nproc)" CXXFLAGS="${EXTRA_CXXFLAGS:-}" > ".cache/logs/make_${name}.log" 2>&1; then
    echo "build failed: $cfg (see .cache/logs/make_${name}.log)" >&2; exit 1
  fi
  test -x "bin/$name" || { echo "missing bin/$name after build" >&2; exit 1; }
  echo "built bin/$name from $cfg"
done
