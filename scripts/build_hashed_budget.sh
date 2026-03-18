#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  bash scripts/build_hashed_budget.sh --budget-kb <24|48|96|192|384|768|1536> [options]

Options:
  --config <path>    ChampSim config to use (default: champsim_config.json)
  --jobs <N>         Parallel make jobs (default: nproc or 4)
  --repo-dir <path>  Repo root (default: script parent)
  --build-prefix <path>
                    Build prefix for this budget (default: builds/champsim_hashed_<budget>kb)
EOF
}

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
CONFIG_PATH="champsim_config.json"
JOBS=""
BUDGET_KB=""
BUILD_PREFIX=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --budget-kb) BUDGET_KB="$2"; shift 2 ;;
    --config) CONFIG_PATH="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --repo-dir) REPO_DIR="$2"; shift 2 ;;
    --build-prefix) BUILD_PREFIX="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$BUDGET_KB" ]]; then
  echo "Error: --budget-kb is required." >&2
  exit 2
fi

case "$BUDGET_KB" in
  24) TABLE_BITS=11 ;;
  48) TABLE_BITS=12 ;;
  96) TABLE_BITS=13 ;;
  192) TABLE_BITS=14 ;;
  384) TABLE_BITS=15 ;;
  768) TABLE_BITS=16 ;;
  1536) TABLE_BITS=17 ;;
  *)
    echo "Error: unsupported budget '$BUDGET_KB'. Expected one of 24 48 96 192 384 768 1536." >&2
    exit 2
    ;;
esac

if [[ -z "$JOBS" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
  else
    JOBS=4
  fi
fi

cd "$REPO_DIR"

if [[ "$CONFIG_PATH" != /* ]]; then
  CONFIG_PATH="$REPO_DIR/$CONFIG_PATH"
fi

if [[ ! -f "$CONFIG_PATH" ]]; then
  echo "Error: config not found: $CONFIG_PATH" >&2
  exit 2
fi

if [[ ! -x "$REPO_DIR/vcpkg/vcpkg" ]]; then
  git submodule update --init
  (cd "$REPO_DIR/vcpkg" && ./bootstrap-vcpkg.sh)
fi

if ! find "$REPO_DIR/vcpkg_installed" -path '*/include/fmt/core.h' -print -quit | grep -q .; then
  "$REPO_DIR/vcpkg/vcpkg" install
fi

EXE_NAME="champsim_hashed_${BUDGET_KB}kb"
if [[ -z "$BUILD_PREFIX" ]]; then
  BUILD_PREFIX="$REPO_DIR/builds/$EXE_NAME"
elif [[ "$BUILD_PREFIX" != /* ]]; then
  BUILD_PREFIX="$REPO_DIR/$BUILD_PREFIX"
fi

OBJ_ROOT="$BUILD_PREFIX/.csconfig"
DEP_ROOT="$OBJ_ROOT"
TMP_CONFIG="$(mktemp "${TMPDIR:-/tmp}/champsim_config_${BUDGET_KB}kb_XXXXXX.json")"
trap 'rm -f "$TMP_CONFIG"' EXIT

python3 - <<'PY' "$CONFIG_PATH" "$TMP_CONFIG" "$EXE_NAME"
import json
import sys

src, dst, exe_name = sys.argv[1:4]
with open(src, "r", encoding="utf-8") as f:
    data = json.load(f)
data["executable_name"] = exe_name
with open(dst, "w", encoding="utf-8") as f:
    json.dump(data, f, indent=2)
    f.write("\n")
PY

./config.sh --prefix "$BUILD_PREFIX" "$TMP_CONFIG"
rm -f absolute.options
make -B absolute.options >/dev/null

echo "Building $EXE_NAME with HASHED_PERCEPTRON_TABLE_BITS=$TABLE_BITS"
make -j"$JOBS" \
  OBJ_ROOT="$OBJ_ROOT" \
  DEP_ROOT="$DEP_ROOT" \
  CPPFLAGS="-DHASHED_PERCEPTRON_TABLE_BITS=$TABLE_BITS" \
  "$BUILD_PREFIX/bin/$EXE_NAME"
