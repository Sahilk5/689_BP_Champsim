#!/usr/bin/env bash
#SBATCH --job-name=hash_run
#SBATCH --get-user-env=L
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=6G
#SBATCH --time=16:00:00

set -euo pipefail

REPO_DIR="${REPO_DIR:-$SLURM_SUBMIT_DIR}"
TRACE_DIR="${TRACE_DIR:?TRACE_DIR is required}"
RUN_DIR="${RUN_DIR:?RUN_DIR is required}"
BUILD_ROOT="${BUILD_ROOT:-$REPO_DIR/builds}"
BENCH_LIST="${BENCH_LIST:-$REPO_DIR/scripts/bench_lists/spec2017_branch_20.txt}"
WARMUP_INS="${WARMUP_INS:-200000000}"
SIM_INS="${SIM_INS:-1000000000}"
MODULES="${MODULES:-GCC/13.2.0 CMake/3.31.8}"

BUDGETS=(24 48 96 192 384 768 1536)

if [[ -n "$MODULES" ]]; then
  # shellcheck disable=SC2206
  module_list=($MODULES)
  if ! type module >/dev/null 2>&1; then
    for init_script in /etc/profile.d/modules.sh /etc/profile.d/lmod.sh /usr/share/Modules/init/bash /usr/share/lmod/lmod/init/bash; do
      if [[ -r "$init_script" ]]; then
        # shellcheck disable=SC1090
        source "$init_script"
        break
      fi
    done
  fi
  module purge
  for mod in "${module_list[@]}"; do
    module load "$mod"
  done
fi

mapfile -t BENCHES < <(grep -vE '^[[:space:]]*(#|$)' "$BENCH_LIST")
NUM_BENCHES="${#BENCHES[@]}"
NUM_BUDGETS="${#BUDGETS[@]}"
TOTAL_TASKS=$((NUM_BENCHES * NUM_BUDGETS))
TASK_ID="${SLURM_ARRAY_TASK_ID:?SLURM_ARRAY_TASK_ID is required}"

if (( TASK_ID < 0 || TASK_ID >= TOTAL_TASKS )); then
  echo "Error: array task $TASK_ID is out of range 0..$((TOTAL_TASKS - 1))." >&2
  exit 2
fi

BENCH_IDX=$((TASK_ID / NUM_BUDGETS))
BUDGET_IDX=$((TASK_ID % NUM_BUDGETS))

TRACE_ENTRY="${BENCHES[$BENCH_IDX]}"
BUDGET_KB="${BUDGETS[$BUDGET_IDX]}"

if [[ "$TRACE_ENTRY" = /* ]]; then
  TRACE_PATH="$TRACE_ENTRY"
else
  TRACE_PATH="${TRACE_DIR%/}/$TRACE_ENTRY"
fi

if [[ ! -f "$TRACE_PATH" ]]; then
  echo "Error: trace not found: $TRACE_PATH" >&2
  exit 2
fi

TRACE_FILE="$(basename "$TRACE_PATH")"
BENCH_NAME="${TRACE_FILE%%-*}"
EXE_PATH="$BUILD_ROOT/champsim_hashed_${BUDGET_KB}kb/bin/champsim_hashed_${BUDGET_KB}kb"

if [[ ! -x "$EXE_PATH" ]]; then
  echo "Error: binary not found or not executable: $EXE_PATH" >&2
  exit 2
fi

RUNLOG_DIR="$RUN_DIR/results/runlogs"
mkdir -p "$RUNLOG_DIR"

STATUS_LOG="$RUNLOG_DIR/${BENCH_NAME}_hashed_${BUDGET_KB}kb.status.log"
STDOUT_LOG="$RUNLOG_DIR/${BENCH_NAME}_hashed_${BUDGET_KB}kb.stdout.log"
STDERR_LOG="$RUNLOG_DIR/${BENCH_NAME}_hashed_${BUDGET_KB}kb.stderr.log"
TIME_LOG="$RUNLOG_DIR/${BENCH_NAME}_hashed_${BUDGET_KB}kb.time.log"

{
  printf 'job_id=%s\n' "${SLURM_JOB_ID:-}"
  printf 'array_task_id=%s\n' "${SLURM_ARRAY_TASK_ID:-}"
  printf 'host=%s\n' "$(hostname)"
  printf 'start_time=%s\n' "$(date)"
  printf 'binary=%s\n' "$EXE_PATH"
  printf 'trace=%s\n' "$TRACE_PATH"
  printf 'budget=%sKB\n' "$BUDGET_KB"
} >"$STATUS_LOG"

set +e
/usr/bin/time -v -o "$TIME_LOG" \
  "$EXE_PATH" \
  --warmup-instructions "$WARMUP_INS" \
  --simulation-instructions "$SIM_INS" \
  "$TRACE_PATH" \
  >"$STDOUT_LOG" 2>"$STDERR_LOG"
RC=$?
set -e

{
  printf 'exit_code=%s\n' "$RC"
  printf 'end_time=%s\n' "$(date)"
} >>"$STATUS_LOG"

if [[ "$RC" -eq 139 ]]; then
  {
    echo "FAIL: ChampSim exited with code 139"
    echo "Detected segmentation fault (exit code 139)"
  } >>"$STDERR_LOG"
fi

exit "$RC"
