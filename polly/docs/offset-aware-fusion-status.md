# Offset-Aware Fusion Status

This note tracks the current state of the Polly changes for automatic fusion of
pipelines lowered from standard-library algorithms whose iteration spaces
differ by small constant offsets.
The validation target is RISC-V.

## Problem Statement

One common source-level pattern is a sequence of standard-library algorithms
that walk the same contiguous storage, but not necessarily over exactly the
same range:

- one pass uses `[0..N)`
- the next uses `[1..N)`
- the next uses `[0..N-1)`

Typical examples are chains of `std::transform`, `std::copy`,
`std::replace_copy`, or similar size-stable algorithms over one array or
vector. Even though such passes are "almost the same loop", baseline Polly does
not reliably fuse them once their statement domains stop matching exactly.

The goal of this work is:

- recognize small constant-offset differences between adjacent statement domains
- let the scheduler treat such statements as fusion candidates
- keep this working for a useful limited class of pipelines lowered from
  standard-library algorithms

This is a pragmatic limited-scope improvement, not a full solution for all
standard-library algorithms.

## Target Pattern

The currently supported target class is:

- several size-stable passes over the same contiguous storage
- affine or almost-affine loop domains
- domains that differ only by small constant offsets such as `[0..N)`,
  `[1..N)`, `[0..N-1)`
- no data-dependent output growth in the stable end-to-end path

Good examples:

- multiple `std::transform` passes
- `std::copy` / `std::transform` / `std::copy`
- `std::iota` / `std::transform` / `std::replace_copy`

Still difficult:

- `std::copy_if(..., std::back_inserter(...))`
- more generally compaction / append / grow patterns

## What Was Implemented

### 1. Offset-aware scheduler fusion

Implemented in:

- [`include/polly/Options.h`](../include/polly/Options.h)
  - `-mllvm -polly-force-offset-fusion`
- [`include/polly/Support/ISLTools.h`](../include/polly/Support/ISLTools.h)
- [`lib/Support/ISLTools.cpp`](../lib/Support/ISLTools.cpp)
- [`lib/Transform/ScheduleOptimizer.cpp`](../lib/Transform/ScheduleOptimizer.cpp)
- [`lib/Transform/ScheduleTreeTransform.cpp`](../lib/Transform/ScheduleTreeTransform.cpp)

This teaches Polly to:

- detect statement domains that differ only by small constant offsets
- add synthetic proximity bonuses for them
- shift outer schedules by a constant before greedy fusion

### 2. Logical-domain recovery and analysis support

Implemented in:

- [`include/polly/ScopBuilder.h`](../include/polly/ScopBuilder.h)
- [`include/polly/ScopInfo.h`](../include/polly/ScopInfo.h)
- [`include/polly/Support/ScopHelper.h`](../include/polly/Support/ScopHelper.h)
- [`lib/Analysis/ScopBuilder.cpp`](../lib/Analysis/ScopBuilder.cpp)
- [`lib/Analysis/ScopInfo.cpp`](../lib/Analysis/ScopInfo.cpp)
- [`lib/Analysis/ScopDetection.cpp`](../lib/Analysis/ScopDetection.cpp)
- [`lib/Support/ScopHelper.cpp`](../lib/Support/ScopHelper.cpp)

This recovers logical offset information from lowered access patterns,
normalizes iterator-style pointer arithmetic, handles PHI-carried pointer
chains, and recognizes compaction-like lowered statements.

### 3. Source-level integration helpers

Implemented in:

- [`include/polly/ScopDetection.h`](../include/polly/ScopDetection.h)
- [`lib/Transform/CodePreparation.cpp`](../lib/Transform/CodePreparation.cpp)
- [`lib/Analysis/ScopDetection.cpp`](../lib/Analysis/ScopDetection.cpp)

This includes:

- no-growth fast-path versioning for `std::vector` append-style lowering
- dedicated `...polly.nogrow.edge` CFG anchors
- experimental synthetic region stitching

### 4. Codegen / runtime-check fixes

Implemented in:

- [`lib/CodeGen/IslExprBuilder.cpp`](../lib/CodeGen/IslExprBuilder.cpp)
- [`lib/CodeGen/IslNodeBuilder.cpp`](../lib/CodeGen/IslNodeBuilder.cpp)

This keeps the Polly path live when wide runtime checks appear, so stable
source-level fusion cases now enter `polly.start` through a real
`%polly.rtc.result` instead of a constant `false` branch.

## Confirmed Working Cases

### Lowered IR

These are considered solid:

- [`test/ScheduleOptimizer/offset-aware-fusion.ll`](../test/ScheduleOptimizer/offset-aware-fusion.ll)
- [`test/ScheduleOptimizer/offset-aware-compaction.ll`](../test/ScheduleOptimizer/offset-aware-compaction.ll)
- [`test/ScopInfo/compaction-pattern.ll`](../test/ScopInfo/compaction-pattern.ll)
- [`test/ScopInfo/inttoptr-phi-iterator.ll`](../test/ScopInfo/inttoptr-phi-iterator.ll)
- [`test/CodeGen/offset-aware-fusion-live-rtc.ll`](../test/CodeGen/offset-aware-fusion-live-rtc.ll)

On the current RISC-V build these still give the expected schedule/codegen
shape, and the live-RTC codegen test passes through `polly-codegen + verify`.

### Source-level friendly source cases

Confirmed on RISC-V:

- [`test/Inputs/stl_like_offset_three_transform.cpp`](../test/Inputs/stl_like_offset_three_transform.cpp)
  - strict harness: `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp)
  - strict harness: `PASS`
  - fused band on 3 statements
  - live `%polly.rtc.result` in codegen
- [`test/Inputs/stl_like_offset_pointer.cpp`](../test/Inputs/stl_like_offset_pointer.cpp)
  - strict harness: `PASS`
  - fused band on 2 statements

### Difficult source-level case

- [`test/Inputs/stl_like_offset_vector.cpp`](../test/Inputs/stl_like_offset_vector.cpp)

This still does not match the desired `copy_if/back_inserter` compaction shape.
It is testable on RISC-V now, but the strict integration check still does not
observe the expected compaction signal or the desired fused result.

## Current Status

The task is still active.

Already achieved:

- Polly can fuse a useful limited class of pipelines lowered from
  standard-library algorithms with small constant-offset iteration-space
  differences.
- This works at the schedule level and end-to-end for stable source-level
  cases such as `std::iota / std::transform / std::replace_copy`.

Not yet finished:

- full source-level fusion of pipelines such as
  `fill / transform / copy_if(back_inserter)` into one larger SCoP
- general support for compaction / append / grow patterns

## Remaining Boundary

The main remaining blocker is path-sensitive region stitching for the versioned
no-growth fast path in `ScopDetection`.

A plain SESE synthetic region still tends to pull in the original slow path and
trips legality checks such as:

- `Loop ... has multiple exits`

So the remaining work is no longer mostly about schedule fusion. It is mostly
about source-level region/path modeling for difficult append-style patterns.

## RISC-V Note

RISC-V validation needs a working sysroot. Friendly source-level cases are
already validated, but hosted container cases depend more strongly on the exact
C/C++ runtime setup.

For source-level validation, the environment should provide:

- a RISC-V-capable LLVM/Polly build
- a usable RISC-V sysroot
- C++ standard library headers matching that sysroot

In practice, both bare-metal and hosted GNU/Linux-style RISC-V setups are
useful:

- a bare-metal setup is enough for the friendly offset-aware fusion cases
- a hosted GNU/Linux-style setup is more representative for container-heavy
  cases such as `std::vector` / `std::back_inserter`

## Reproduction

For the current RISC-V workflow, the minimum command shape is:

```bash
python3 utils/check_stl_like_fusion.py \
  --strict \
  --example three_transform \
  --opt ../build-rv-polly/bin/opt \
  --clangxx ../build-rv-polly/bin/clang++ \
  --target riscv64-unknown-elf \
  --sysroot /opt/homebrew/Cellar/riscv-gnu-toolchain/main/riscv64-unknown-elf \
  --gcc-toolchain /opt/homebrew/opt/riscv-gnu-toolchain
```

`--plugin` can be omitted in that setup because `build-rv-polly/bin/opt`
already contains Polly.

To preserve generated `*.ll`, `scops.txt`, `schedule.txt`, `debug.txt`, and
`optimized.ll`, pass `--keep-dir <path>` to the integration harness. These are
derived artifacts and are intentionally not committed to the repository.
