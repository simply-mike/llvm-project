# Offset-Aware Fusion Status

This note tracks the current state of the Polly changes for automatic fusion of
STL-lowered pipelines whose iteration spaces differ by small constant offsets.

## Problem Statement

One common source-level pattern is a sequence of STL algorithms that all walk
the same contiguous buffer, but not necessarily over exactly the same range:

- one pass may use `[0..N)`
- the next may use `[1..N)`
- the next may use `[0..N-1)`

Typical examples are chains of `std::transform`, `std::copy`,
`std::replace_copy`, or similar size-stable algorithms over one array or
vector. Even though such passes are "almost the same loop", baseline Polly does
not reliably fuse them once their statement domains stop matching exactly.

The goal of this work is therefore:

- teach Polly to recognize small constant-offset differences between adjacent
  statement domains
- let the scheduler treat such statements as fusion candidates
- keep this working at least for a useful limited class of STL-lowered
  pipelines, even if the full general source-level problem remains open

This work explicitly started as a pragmatic, limited-scope improvement rather
than a full solution for all STL algorithms.

## Target Pattern

The main target class is:

- several size-stable passes over the same contiguous storage
- affine or almost-affine loop domains
- domains that differ only by small constant offsets such as `[0..N)`,
  `[1..N)`, `[0..N-1)`
- no data-dependent output growth in the stable end-to-end path

Good examples:

- multiple `std::transform` passes
- `std::copy` / `std::transform` / `std::copy`
- `std::iota` / `std::transform` / `std::replace_copy`

Harder class, still not finished:

- `std::copy_if(..., std::back_inserter(...))`
- more generally compaction / append / grow patterns

## High-Level Approach

I did not try to make Polly recognize STL algorithms by name. Instead, the
implementation works at the polyhedral level after source-level STL code has
been lowered into loops and memory accesses.

The work was split into four layers:

1. scheduler-side offset-aware fusion
2. SCoP analysis improvements so Polly keeps enough logical offset information
3. source-level integration helpers for STL-like lowering
4. codegen fixes so the generated Polly path remains live

## What Was Implemented

### 1. Offset-aware scheduler fusion

Added a new option:

- [`include/polly/Options.h`](../include/polly/Options.h)
  - `-mllvm -polly-force-offset-fusion`

Added helpers to recognize compatible constant-offset domains:

- [`include/polly/Support/ISLTools.h`](../include/polly/Support/ISLTools.h)
- [`lib/Support/ISLTools.cpp`](../lib/Support/ISLTools.cpp)

Extended the scheduler so such domains receive a synthetic proximity bonus:

- [`lib/Transform/ScheduleOptimizer.cpp`](../lib/Transform/ScheduleOptimizer.cpp)

Extended greedy fusion so outer schedules can be shifted by a constant before
fusion:

- [`lib/Transform/ScheduleTreeTransform.cpp`](../lib/Transform/ScheduleTreeTransform.cpp)

This is the core reason Polly can now fuse statements whose domains differ by a
small constant offset instead of requiring exact equality.

### 2. Logical-domain recovery and analysis support

Source-level STL lowering often hides the original logical index structure
behind pointer arithmetic, PHIs, and `inttoptr(ptrtoint(...)+C)` patterns.

To keep such loops usable by Polly, I added analysis support in:

- [`include/polly/ScopBuilder.h`](../include/polly/ScopBuilder.h)
- [`include/polly/ScopInfo.h`](../include/polly/ScopInfo.h)
- [`include/polly/Support/ScopHelper.h`](../include/polly/Support/ScopHelper.h)
- [`lib/Analysis/ScopBuilder.cpp`](../lib/Analysis/ScopBuilder.cpp)
- [`lib/Analysis/ScopInfo.cpp`](../lib/Analysis/ScopInfo.cpp)
- [`lib/Analysis/ScopDetection.cpp`](../lib/Analysis/ScopDetection.cpp)
- [`lib/Support/ScopHelper.cpp`](../lib/Support/ScopHelper.cpp)

This work included:

- recovering logical offset information from lowered access patterns
- normalizing iterator-like pointer arithmetic
- handling PHI-carried pointer chains
- recognizing compaction-like lowered statements
- adding no-growth helpers for `back_inserter`-style lowering

### 3. Source-level integration path

For source-level STL cases, especially `std::vector` plus `back_inserter`, the
main issue was not just the scheduler but also CFG shape and region formation.

I therefore added preparation and region-handling infrastructure in:

- [`include/polly/ScopDetection.h`](../include/polly/ScopDetection.h)
- [`lib/Transform/CodePreparation.cpp`](../lib/Transform/CodePreparation.cpp)
- [`lib/Analysis/ScopDetection.cpp`](../lib/Analysis/ScopDetection.cpp)

This enabled:

- no-growth fast-path versioning for `std::vector` append-style lowering
- dedicated `...polly.nogrow.edge` anchoring in the CFG
- experimental synthetic region stitching infrastructure

This part was necessary to make progress on the harder source-level cases, even
though it does not yet finish the full `copy_if(back_inserter)` problem.

### 4. Codegen / runtime-check fixes

Even after schedule fusion was working, the generated Polly path could still be
disabled by a constant `false` dispatch when wide runtime-check expressions
appeared.

This was fixed in:

- [`lib/CodeGen/IslExprBuilder.cpp`](../lib/CodeGen/IslExprBuilder.cpp)
- [`lib/CodeGen/IslNodeBuilder.cpp`](../lib/CodeGen/IslNodeBuilder.cpp)

Result:

- stable source-level fusion cases now generate a live `%polly.rtc.result`
- the program enters `polly.start` through a real runtime check instead of a
  constant `false` branch

## Confirmed Working Cases

### Lowered IR

These cases are considered solid:

- [`test/ScheduleOptimizer/offset-aware-fusion.ll`](../test/ScheduleOptimizer/offset-aware-fusion.ll)
  - scheduler-side offset-aware fusion on lowered affine loops
- [`test/ScheduleOptimizer/offset-aware-compaction.ll`](../test/ScheduleOptimizer/offset-aware-compaction.ll)
  - lowered compaction-like pipeline with a fused producer/compaction band
- [`test/ScopInfo/compaction-pattern.ll`](../test/ScopInfo/compaction-pattern.ll)
  - compaction-like pattern recognition
- [`test/ScopInfo/inttoptr-phi-iterator.ll`](../test/ScopInfo/inttoptr-phi-iterator.ll)
  - iterator / `inttoptr` normalization support

### Source-level friendly STL-like cases

- [`test/Inputs/stl_like_offset_three_transform.cpp`](../test/Inputs/stl_like_offset_three_transform.cpp)
  - three `std::transform` passes
  - strict harness result: fused band on 3 statements
- [`test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp)
  - `std::iota` / `std::transform` / `std::replace_copy`
  - strict harness result: fused band on 3 statements
  - stable end-to-end codegen path with live `%polly.rtc.result`
- [`test/Inputs/stl_like_offset_pointer.cpp`](../test/Inputs/stl_like_offset_pointer.cpp)
  - source-level raw-pointer STL-like chain
  - strict harness result: fused band on 2 statements

### Codegen regression

- [`test/CodeGen/offset-aware-fusion-live-rtc.ll`](../test/CodeGen/offset-aware-fusion-live-rtc.ll)
  - verifies that the stable 3-way case still emits a live runtime check and
    enters `polly.start` via `%polly.rtc.result`

### Partially working difficult case

- [`test/Inputs/stl_like_offset_vector.cpp`](../test/Inputs/stl_like_offset_vector.cpp)
  - source-level `std::vector` + `std::back_inserter`
  - current strict harness result: separate no-growth compaction SCoP with a
    fused band on 4 statements and a dedicated `...exit.polly.nogrow.edge`
  - this is real progress, but it is still not the final one-loop fusion target

## Main Problems I Hit

### Exact domain equality was too strict

Baseline Polly works best when statement domains match exactly. That is too
strict for common STL-lowered patterns with tiny range differences.

### STL lowering hides the logical loop structure

Even simple-looking source can turn into:

- pointer addrecs
- PHI-carried iterators
- `inttoptr(ptrtoint(base)+offset)` forms
- non-obvious loop bounds

Without extra normalization, Polly often misses the fact that these statements
belong to the same logical loop family.

### `copy_if(back_inserter)` is not a regular affine write

This remains the hardest case because:

- output position depends on data
- output size is not fixed in advance
- `std::vector` growth introduces extra CFG, state, and allocation logic

That means the pattern is closer to stream compaction plus append/grow
semantics than to a plain affine map.

### Schedule fusion alone is not enough

Even when the scheduler wants fusion, source-level cases can still fail due to:

- region formation
- multiple exits
- path sensitivity
- runtime-check shaping
- codegen disabling the Polly path

## Current Status

The task is still active.

What is already achieved:

- Polly can now fuse a useful limited class of STL-lowered pipelines with
  small constant-offset iteration-space differences.
- This works not only at the schedule level, but also end-to-end for the
  stable 3-way source-level case:
  `std::iota` / `std::transform` / `std::replace_copy`.

What is not yet finished:

- full source-level fusion of pipelines such as
  `fill / transform / copy_if(back_inserter)` into one larger SCoP
- general support for compaction / append / grow patterns

## Remaining Boundary

The main remaining blocker is path-sensitive region stitching for the
versioned no-growth fast path in `ScopDetection`.

A plain SESE synthetic region still tends to pull in the original slow path and
trips legality checks such as:

- `Loop ... has multiple exits`

So the remaining work is no longer mostly about schedule fusion. It is mostly
about source-level region/path modeling for difficult append-style patterns.

## Reproduction

```bash
ninja -C build-standalone-cxx17 LLVMPolly

python3 utils/check_stl_like_fusion.py \
  --strict \
  --example three_transform \
  --plugin build-standalone-cxx17/lib/LLVMPolly.so \
  --opt ../build/bin/opt \
  --clangxx clang++

python3 utils/check_stl_like_fusion.py \
  --strict \
  --example iota_transform_replace_copy \
  --plugin build-standalone-cxx17/lib/LLVMPolly.so \
  --opt ../build/bin/opt \
  --clangxx clang++

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

To preserve generated `*.ll`, `scops.txt`, `schedule.txt`, `debug.txt`, and
`optimized.ll`, pass `--keep-dir <path>` to the integration harness. These are
derived artifacts and are intentionally not committed to the repository.
