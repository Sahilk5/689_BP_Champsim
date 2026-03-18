#!/usr/bin/env bash
#SBATCH --job-name=hash_build
#SBATCH --get-user-env
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=4
#SBATCH --mem=64G
#SBATCH --time=04:00:00

set -euo pipefail

REPO_DIR="${REPO_DIR:-$SLURM_SUBMIT_DIR}"
CONFIG_PATH="${CONFIG_PATH:-$REPO_DIR/champsim_config.json}"
BUILD_LOG_DIR="${BUILD_LOG_DIR:-$REPO_DIR/build_logs}"
BUILD_ROOT="${BUILD_ROOT:-$REPO_DIR/builds}"
BUDGETS="${BUDGETS:-24 48 96 192 384 768 1536}"
MODULES="${MODULES:-GCC/13.2.0 CMake/3.31.8}"

mkdir -p "$BUILD_LOG_DIR"
cd "$REPO_DIR"

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

for budget in $BUDGETS; do
  log_file="$BUILD_LOG_DIR/build_${budget}kb.log"
  build_prefix="$BUILD_ROOT/champsim_hashed_${budget}kb"
  echo "=== Building ${budget}KB ==="
  bash scripts/build_hashed_budget.sh \
    --repo-dir "$REPO_DIR" \
    --config "$CONFIG_PATH" \
    --jobs "${SLURM_CPUS_PER_TASK:-8}" \
    --build-prefix "$build_prefix" \
    --budget-kb "$budget" |& tee "$log_file"
done
