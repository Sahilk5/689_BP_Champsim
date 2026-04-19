# All-Three Ablation

This variant is derived from `wisard_base_3ea074e` and combines the three isolated ablation changes into a single base-derived predictor.

## Difference From Base

- The base predictor shuffles one flat index list across the weighted 2158-bit feature pool.
- This variant changes the tuple mapping to weighted family sampling across the same 5 base feature families.
- It widens the stored speculative and real global history from 24 bits to 64 bits, while still exposing only the low 24 bits to the original base feature encoder.
- It additionally carries speculative and real recency/modulo-history side state and restores those structures on mispredict, matching the bookkeeping style used in `wisard_v1`.

## What Stays Base-Like

- The feature construction path is still the original base 2158-bit weighted feature pool.
- The extra speculative side state is not consumed by the feature encoder.
- The main WiSARD table geometry, bleaching, and training path remain the base path.

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
- The study runner writes `launcher.log`, `.json`, `.log`, and `.time` files into a timestamped subdirectory there.
