# Base Raw Pool With Flat Mapping

This variant is derived from `wisard_base_3ea074e`.

## Difference From Base

- Replaces the repeated `2158`-bit weighted feature pool with the compact raw base pool.
- Keeps the exact base flat shuffled tuple mapping.
- Keeps the exact base update, bleaching, decay, and history behavior.

## Raw Feature Pool

- `PC`: `24`
- `GHR`: `24`
- `PC xor GHR`: `24`
- `LHTs`: `24 + 16 + 9 + 7 + 5 = 61`
- `PATH`: `64`
- Total: `197` bits

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
