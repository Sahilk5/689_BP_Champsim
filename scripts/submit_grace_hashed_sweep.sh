#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  bash scripts/submit_grace_hashed_sweep.sh --trace-dir <path> --run-dir <path> [options]

Required:
  --trace-dir <path>   Directory containing *.champsimtrace.xz files
  --run-dir <path>     Output directory for logs and per-run artifacts

Options:
  --repo-dir <path>    ChampSim repo root (default: script parent)
  --build-root <path>  Root directory for per-budget builds
                      (default: $SCRATCH/champsim/<repo>/builds if $SCRATCH exists, else <repo>/builds)
  --config <path>      Config file to build from (default: champsim_config.json)
  --bench-list <path>  Bench list file (default: scripts/bench_lists/spec2017_branch_20.txt)
  --warmup-ins <N>     Warmup instructions (default: 200000000)
  --sim-ins <N>        Simulation instructions (default: 1000000000)
  --partition <name>   Slurm partition
  --account <name>     Slurm account
  --qos <name>         Slurm QoS
  --build-time <hh:mm:ss> Build job time limit (default: 04:00:00)
  --run-time <hh:mm:ss>   Run job time limit (default: 16:00:00)
  --build-mem <size>   Build job memory (default: 64G)
  --run-mem <size>     Run job memory (default: 6G)
  --build-cpus <N>     Build job CPUs (default: 4)
  --run-cpus <N>       Run job CPUs (default: 1)
  --module <name>      Module to load inside jobs; repeatable
                      (default modules on Grace: GCC/13.2.0 and CMake/3.31.8)
EOF
}

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
TRACE_DIR=""
RUN_DIR=""
CONFIG_PATH=""
BENCH_LIST=""
BUILD_ROOT=""
WARMUP_INS="200000000"
SIM_INS="1000000000"
PARTITION=""
ACCOUNT=""
QOS=""
BUILD_TIME="04:00:00"
RUN_TIME="16:00:00"
BUILD_MEM="64G"
RUN_MEM="6G"
BUILD_CPUS="4"
RUN_CPUS="1"
MODULES=("GCC/13.2.0" "CMake/3.31.8")
MODULES_EXPLICIT=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --trace-dir) TRACE_DIR="$2"; shift 2 ;;
    --run-dir) RUN_DIR="$2"; shift 2 ;;
    --repo-dir) REPO_DIR="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    --config) CONFIG_PATH="$2"; shift 2 ;;
    --bench-list) BENCH_LIST="$2"; shift 2 ;;
    --warmup-ins) WARMUP_INS="$2"; shift 2 ;;
    --sim-ins) SIM_INS="$2"; shift 2 ;;
    --partition) PARTITION="$2"; shift 2 ;;
    --account) ACCOUNT="$2"; shift 2 ;;
    --qos) QOS="$2"; shift 2 ;;
    --build-time) BUILD_TIME="$2"; shift 2 ;;
    --run-time) RUN_TIME="$2"; shift 2 ;;
    --build-mem) BUILD_MEM="$2"; shift 2 ;;
    --run-mem) RUN_MEM="$2"; shift 2 ;;
    --build-cpus) BUILD_CPUS="$2"; shift 2 ;;
    --run-cpus) RUN_CPUS="$2"; shift 2 ;;
    --module)
      if (( MODULES_EXPLICIT == 0 )); then
        MODULES=()
        MODULES_EXPLICIT=1
      fi
      MODULES+=("$2")
      shift 2
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2 ;;
  esac
done

if [[ -z "$TRACE_DIR" || -z "$RUN_DIR" ]]; then
  echo "Error: --trace-dir and --run-dir are required." >&2
  usage
  exit 2
fi

REPO_DIR="$(cd "$REPO_DIR" && pwd -P)"
TRACE_DIR="$(cd "$TRACE_DIR" && pwd -P)"
mkdir -p "$RUN_DIR"
RUN_DIR="$(cd "$RUN_DIR" && pwd -P)"

if [[ -z "$CONFIG_PATH" ]]; then
  CONFIG_PATH="$REPO_DIR/champsim_config.json"
elif [[ "$CONFIG_PATH" != /* ]]; then
  CONFIG_PATH="$REPO_DIR/$CONFIG_PATH"
fi

if [[ -z "$BENCH_LIST" ]]; then
  BENCH_LIST="$REPO_DIR/scripts/bench_lists/spec2017_branch_20.txt"
elif [[ "$BENCH_LIST" != /* ]]; then
  BENCH_LIST="$REPO_DIR/$BENCH_LIST"
fi

if [[ -z "$BUILD_ROOT" ]]; then
  if [[ -n "${SCRATCH:-}" ]]; then
    BUILD_ROOT="$SCRATCH/champsim/$(basename "$REPO_DIR")/builds"
  else
    BUILD_ROOT="$REPO_DIR/builds"
  fi
elif [[ "$BUILD_ROOT" != /* ]]; then
  BUILD_ROOT="$REPO_DIR/$BUILD_ROOT"
fi

mkdir -p "$RUN_DIR/logs" "$RUN_DIR/results/runlogs" "$RUN_DIR/build_logs"
mkdir -p "$BUILD_ROOT"

BENCH_COUNT="$(grep -vcE '^[[:space:]]*(#|$)' "$BENCH_LIST")"
ARRAY_MAX=$((BENCH_COUNT * 7 - 1))

SBATCH_COMMON=()
SBATCH_COMMON+=(--get-user-env=L)
[[ -n "$PARTITION" ]] && SBATCH_COMMON+=(--partition "$PARTITION")
[[ -n "$ACCOUNT" ]] && SBATCH_COMMON+=(--account "$ACCOUNT")
[[ -n "$QOS" ]] && SBATCH_COMMON+=(--qos "$QOS")

MODULES_STR=""
if (( ${#MODULES[@]} > 0 )); then
  MODULES_STR="${MODULES[*]}"
fi

BUILD_JOB_ID="$(
  sbatch --parsable \
    "${SBATCH_COMMON[@]}" \
    --cpus-per-task "$BUILD_CPUS" \
    --mem "$BUILD_MEM" \
    --time "$BUILD_TIME" \
    --output "$RUN_DIR/logs/build_%j.out" \
    --error "$RUN_DIR/logs/build_%j.err" \
    --export=ALL,REPO_DIR="$REPO_DIR",CONFIG_PATH="$CONFIG_PATH",BUILD_LOG_DIR="$RUN_DIR/build_logs",BUILD_ROOT="$BUILD_ROOT",MODULES="$MODULES_STR" \
    "$REPO_DIR/scripts/slurm_build_hashed_sweep.sbatch"
)"

RUN_JOB_ID="$(
  sbatch --parsable \
    "${SBATCH_COMMON[@]}" \
    --dependency "afterok:$BUILD_JOB_ID" \
    --array "0-$ARRAY_MAX" \
    --cpus-per-task "$RUN_CPUS" \
    --mem "$RUN_MEM" \
    --time "$RUN_TIME" \
    --output "$RUN_DIR/logs/hash_%A_%a.out" \
    --error "$RUN_DIR/logs/hash_%A_%a.err" \
    --export=ALL,REPO_DIR="$REPO_DIR",TRACE_DIR="$TRACE_DIR",RUN_DIR="$RUN_DIR",BUILD_ROOT="$BUILD_ROOT",BENCH_LIST="$BENCH_LIST",WARMUP_INS="$WARMUP_INS",SIM_INS="$SIM_INS",MODULES="$MODULES_STR" \
    "$REPO_DIR/scripts/slurm_run_hashed_sweep.sbatch"
)"

echo "Submitted build job: $BUILD_JOB_ID"
echo "Submitted run array: $RUN_JOB_ID"
echo "Run dir: $RUN_DIR"
