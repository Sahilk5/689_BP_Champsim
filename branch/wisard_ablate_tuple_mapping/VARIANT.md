# Tuple Mapping Ablation

This variant is derived from `wisard_base_3ea074e` and changes only the tuple mapping strategy.

## Difference From Base

- The base predictor shuffles one flat index list across the entire 2158-bit weighted feature pool.
- This variant splits that same base feature pool into 5 families and assigns tuple bits by weighted family sampling.
- The base feature encoding, history lengths, speculative update path, bleaching logic, and training logic are unchanged.

## Feature Families

- `PC`: 576 bits
- `GHR`: 288 bits
- `PC xor GHR`: 288 bits
- `LOCAL`: 494 bits
- `PATH`: 512 bits

## Family Weights

- `PC`: 1
- `GHR`: 5
- `PC xor GHR`: 5
- `LOCAL`: 4
- `PATH`: 4

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
- The study runner writes `launcher.log`, `.json`, `.log`, and `.time` files into a timestamped subdirectory there.
