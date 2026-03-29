# Offset-Aware Fusion Status

This note records the current state of the offset-aware fusion work for
STL-lowered pipelines in Polly.

## Confirmed working cases

- Lowered affine loops with constant-offset domains such as `[0..N)`,
  `[1..N)`, `[0..N-1)`.
- Source-level STL-like raw-pointer pipelines when all passes lower to affine
  loop statements.
- Source-level no-growth fast path detection for `std::vector` +
  `std::back_inserter`, including a dedicated `...polly.nogrow.edge` CFG anchor.

## Confirmed examples

- `test/ScheduleOptimizer/offset-aware-fusion.ll`
  Scheduler-side offset-aware fusion on lowered affine loops.
- `test/ScheduleOptimizer/offset-aware-compaction.ll`
  Lowered compaction-like pipeline with a fused producer/compaction band.
- `test/Inputs/stl_like_offset_pointer.cpp`
  Source-level friendly STL-like chain. Current strict harness result:
  fused band on 2 statements.
- `test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`
  Source-level friendly 3-way STL chain with three different algorithms
  (`std::iota`, `std::transform`, `std::replace_copy`). Current strict harness
  result:
  `[{ Stmt6[i0] -> [(i0)]; Stmt4[i0] -> [(1 + i0)]; Stmt2[i0] -> [(i0)] }]`
  i.e. a single fused band on 3 statements.
- `/private/tmp/polly_three_transform_nonconst.cpp`
  Ad-hoc source-level check with three affine-friendly `std::transform` calls.
  Current schedule result:
  `[{ Stmt6[i0] -> [(i0)]; Stmt4[i0] -> [(1 + i0)]; Stmt2[i0] -> [(i0)] }]`
- `test/Inputs/stl_like_offset_vector.cpp`
  Source-level `std::vector` + `std::back_inserter` pipeline. Current strict
  harness result: separate no-growth compaction SCoP with fused band on 4
  statements and a dedicated `...exit.polly.nogrow.edge`.

## Current boundary

- Full source-level fusion of `fill/transform/copy_if(back_inserter)` into one
  larger SCoP is not finished yet.
- The remaining blocker is path-sensitive region stitching in `ScopDetection`.
  A plain SESE synthetic region still pulls in the original slow path and trips
  the `Loop ... has multiple exits` legality check.

## Useful commands

```bash
ninja -C build-standalone-cxx17 LLVMPolly

python3 utils/check_stl_like_fusion.py \
  --strict \
  --example pointer \
  --plugin build-standalone-cxx17/lib/LLVMPolly.so \
  --opt ../build/bin/opt \
  --clangxx clang++

python3 utils/check_stl_like_fusion.py \
  --strict \
  --example vector \
  --plugin build-standalone-cxx17/lib/LLVMPolly.so \
  --opt ../build/bin/opt \
  --clangxx clang++
```
