# Raw Base Pool Plus True MPP4 And IMLI-lite With Weighted Family Sampling

This variant is derived from `wisard_base_3ea074e`.

## Difference From Base

- Replaces the repeated `2158`-bit base feature pool with the compact raw base pool.
- Appends only the true `MPP4 + IMLI-lite` feature families.
- Replaces flat mapping with weighted-family tuple sampling across the raw base and MPP families.
- Keeps the original WiSARD table geometry, decay, bleaching, and 24-bit carried GHR.

## Feature Families

- `PC`: `24`
- `GHR`: `24`
- `PC xor GHR`: `24`
- `LOCAL`: `61`
- `PATH`: `64`
- `MODHIST`: `17`
- `GHISTPATH`: `72`
- `BLURRYPATH`: `43`
- `RECENCYPOS`: `6`
- `IMLI-lite`: `7`
- Total: `342` bits

## Family Weights

- `PC`: `1`
- `GHR`: `5`
- `PC xor GHR`: `5`
- `LOCAL`: `4`
- `PATH`: `4`
- `MODHIST`: `3`
- `GHISTPATH`: `3`
- `BLURRYPATH`: `2`
- `RECENCYPOS`: `1`
- `IMLI-lite`: `1`

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
