# Base Plus True MPP4 And IMLI-lite

This variant is derived from `wisard_base_3ea074e` and keeps the traditional weighted base WiSARD feature pool intact.

## Difference From Base

- The original base `2158`-bit weighted feature pool is preserved exactly.
- This variant appends true MPP-derived feature bits taken from the `wisard_v1_mpp4_weighted_imli_lite` line:
  - `MODHIST`
  - `GHISTPATH`
  - `BLURRYPATH`
  - `RECENCYPOS`
  - `IMLI-lite`
- Tuple mapping stays base-like: one flat shuffled index list over the expanded combined pool.
- The added state is only the minimal state needed to generate those true MPP features.

## What Stays Base-Like

- Base weighted WiSARD feature construction
- Base flat tuple mapping style
- Base table geometry, bleaching, decay, and local history update path
- Base 24-bit carried GHR and base path-history mask behavior

## Benchmark Outputs

- Place run outputs under `benchmark_outputs/`.
- The study runner writes `launcher.log`, `.json`, `.log`, and `.time` files into a timestamped subdirectory there.

## Current Result

- Completed all-available local SPEC-style run:
  - warmup: `50M`
  - simulation: `100M`
  - traces: `11`
  - output folder:
    - `benchmark_outputs/spec_all_available_50m_100m_20260418_131106`
- Aggregate result:
  - IPC: `0.766586107`
  - total MPKI: `6.006307202`
  - conditional MPKI: `5.581720843`

## Comparison

- Versus exact historical base `wisard_base_3ea074e`:
  - IPC: `+1.2681%`
  - total MPKI: `-15.8741%`
  - conditional MPKI: `-16.8778%`
- Versus the older `wisard_v1_mpp4_weighted_imli_lite` run:
  - branch accuracy is almost the same
  - IPC is still much lower

## Why It Does Not Match `wisard_v1_mpp4_weighted_imli_lite`

- This variant is intentionally base-like, while the old `wisard_v1_mpp4_weighted_imli_lite` is not.
- Key structural differences that remain:
  - this variant keeps the weighted `2158`-bit base pool, while `wisard_v1` uses a compact raw feature pool
  - this variant keeps flat shuffled tuple mapping, while `wisard_v1` uses weighted family sampling
  - the old `wisard_v1_mpp4_weighted_imli_lite` binary predates the later `rich_longhist` change, so its relevant advantages are the compact raw pool and weighted family sampling, not the later long-history family
- Practical coverage difference:
  - this variant has `2303` candidate bits for `640` tuple slots
  - the old `wisard_v1_mpp4_weighted_imli_lite` shape had about `342` candidate bits for the same `640` tuple slots
- Expected MPP-family coverage is therefore much denser in the old line:
  - old weighted-family sampler: about `66` tuple bits each for `MODHIST` and `GHISTPATH`, `44` for `BLURRYPATH`, and about `22` each for `RECENCYPOS` and `IMLI-lite`
  - this exact-base flat sampler: only about `5`, `20`, `12`, `2`, and `2` tuple bits respectively
- So this variant shows what true MPP features do on top of exact base, but it is not expected to reproduce the old `wisard_v1` line exactly.
