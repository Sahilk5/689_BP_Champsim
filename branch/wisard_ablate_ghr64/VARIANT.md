# GHR64 Ablation

This variant is derived from `wisard_base_3ea074e` and changes only the stored global history length.

## Difference From Base

- The base predictor stores speculative and real global history as 24-bit bitsets.
- This variant widens those stored histories to 64 bits.
- The feature construction path still uses only the low 24 history bits, so the base feature pool and tuple mapping remain unchanged.

## Expected Effect

- This is an isolation/control variant for carrying deeper history state without exposing new history-derived features.
- Because only the low 24 bits are still consumed by the tuple encoder, results are expected to be very close to the base predictor unless the longer carried history interacts with control flow indirectly.

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
- The study runner writes `launcher.log`, `.json`, `.log`, and `.time` files into a timestamped subdirectory there.
