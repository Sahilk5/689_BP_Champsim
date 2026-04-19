# Speculative State Ablation

This variant is derived from `wisard_base_3ea074e` and changes only the speculative side-state handling.

## Difference From Base

- The base predictor only speculatively updates `spec_global_history` and restores that state on mispredict.
- This variant additionally maintains speculative and real copies of:
  - a recency stack
  - modulo-history buffers for mod-5 and mod-7
- These extra structures are updated on prediction and restored on mispredict, matching the style of `wisard_v1`.
- The address construction path remains the base path and does not consume these extra structures.

## Expected Effect

- This is an isolation/control variant.
- Because the extra speculative structures are not fed into the feature vector, benchmark results are expected to be very close to the base predictor unless the bookkeeping itself perturbs behavior indirectly.

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
- The study runner writes `launcher.log`, `.json`, `.log`, and `.time` files into a timestamped subdirectory there.
