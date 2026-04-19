# Base Raw Pool With Weighted Family Sampling

This variant is derived from `wisard_base_3ea074e`.

## Difference From Base

- Replaces the repeated `2158`-bit weighted feature pool with the compact raw base pool.
- Replaces the base flat shuffled tuple mapping with weighted family sampling across the same 5 base feature families.
- Keeps the exact base update, bleaching, decay, and history behavior.

## Raw Feature Families

- `PC`: `24`
- `GHR`: `24`
- `PC xor GHR`: `24`
- `LOCAL`: `61`
- `PATH`: `64`

## Family Weights

- `PC`: `1`
- `GHR`: `5`
- `PC xor GHR`: `5`
- `LOCAL`: `4`
- `PATH`: `4`

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
