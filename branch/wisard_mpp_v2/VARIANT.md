# WiSARD MPP v2

This is the cleaned-up final base-anchored MPP variant.

## Design Intent

- start from `wisard_base_3ea074e`
- keep the original repeated base feature pool and flat tuple mapping
- add only the true MPP features that helped in the ablation study:
  - `MODHIST`
  - `GHISTPATH`
  - `BLURRYPATH`
  - `RECENCYPOS`
  - `IMLI-lite`

## Why This Replaced The Older `wisard_mpp_v2`

- the previous `wisard_mpp_v2` folder was an exploratory synthetic-MPP variant
- this cleaned-up version is the final minimal design that actually matched the
  strong `base + true MPP` ablation result
- table geometry, bleaching, decay, and flat mapping remain base-like on purpose

## Benchmarked Result

- run: `50M` warmup / `100M` simulation / `11` traces
- IPC: `0.766586107`
- total MPKI: `6.006307202`
- conditional MPKI: `5.581720843`

## Comparison To Exact Base

- versus `wisard_base_3ea074e`
  - IPC: `+1.2681%`
  - total MPKI: `-15.8741%`
  - conditional MPKI: `-16.8778%`
