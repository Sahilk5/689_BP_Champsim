# Raw Base Pool Plus True MPP4 And IMLI-lite With Flat Mapping

This variant is derived from `wisard_base_3ea074e`.

## Difference From Base

- Replaces the repeated `2158`-bit base feature pool with the compact raw base pool.
- Appends only the true `MPP4 + IMLI-lite` feature families.
- Keeps a single flat shuffled tuple mapping across the full compact raw-plus-MPP pool.
- Keeps the original WiSARD table geometry, decay, bleaching, and 24-bit carried GHR.

## Feature Pool

- Raw base features: `197` bits
- `MODHIST`: `17` bits
- `GHISTPATH`: `72` bits
- `BLURRYPATH`: `43` bits
- `RECENCYPOS`: `6` bits
- `IMLI-lite`: `7` bits
- Total: `342` bits

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
