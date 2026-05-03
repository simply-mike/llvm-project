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

- multiple `std::transform` passes, including longer chains
- `std::iota` / `std::transform` / `std::replace_copy`

Still difficult:

- `std::copy_if(..., std::back_inserter(...))`
- copy/fill chains that lower to `memcpy` / `memmove` / `memset` and need
  typed expansion before they can participate in the same fused band
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

The baseline contains focused lowered-IR regressions for the main mechanisms:

- [`test/ScheduleOptimizer/offset-aware-fusion.ll`](../test/ScheduleOptimizer/offset-aware-fusion.ll)
- [`test/ScheduleOptimizer/offset-aware-compaction.ll`](../test/ScheduleOptimizer/offset-aware-compaction.ll)
- [`test/ScopInfo/compaction-pattern.ll`](../test/ScopInfo/compaction-pattern.ll)
- [`test/ScopInfo/inttoptr-phi-iterator.ll`](../test/ScopInfo/inttoptr-phi-iterator.ll)
- [`test/CodeGen/offset-aware-fusion-live-rtc.ll`](../test/CodeGen/offset-aware-fusion-live-rtc.ll)

The lowered-IR regression set has also been refreshed against the current
RISC-V Polly build. The previous stale `check-polly` failures in this area were
expectation drift, not new offset-fusion codegen crashes.

### Source-level friendly source cases

Confirmed on RISC-V:

- [`test/Inputs/stl_like_offset_three_transform.cpp`](../test/Inputs/stl_like_offset_three_transform.cpp)
  - strict harness: `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_four_transform.cpp`](../test/Inputs/stl_like_offset_four_transform.cpp)
  - strict harness: `PASS`
  - fused band on 4 statements
  - `polly-codegen + verify`: `PASS`
- [`test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp)
  - strict harness: `PASS`
  - fused band on 3 statements
  - live `%polly.rtc.result` in codegen
- [`test/Inputs/stl_like_offset_pointer.cpp`](../test/Inputs/stl_like_offset_pointer.cpp)
  - strict harness: `PASS`
  - fused band on 3 statements

### Difficult source-level case

- [`test/Inputs/stl_like_offset_vector.cpp`](../test/Inputs/stl_like_offset_vector.cpp)

This is still treated as a frontier case rather than part of the supported
size-stable class. It is testable on RISC-V and the current strict harness
observes a compaction signal, but that does not amount to general support for
`copy_if(back_inserter)` / append / grow patterns.

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

Current branch policy:

- this branch is kept as the non-experimental RISC-V-focused baseline for the
  currently selected size-stable offset-fusion class
- later class expansions and compaction experiments are kept on
  `llvm-polly-research-experimental`
- personal diary-style notes are intentionally not tracked in this branch

## Correctness Audit Notes

The manual audit of the baseline patch-set focused on transformations that
could change program semantics, not only on missed fusion opportunities.

Scheduler-side offset-aware fusion is comparatively low risk: it adds proximity
and constant-shift opportunities, but validity dependences still constrain the
resulting schedule. In other words, the scheduler is encouraged to place
compatible statements together, but it is not allowed to violate computed
dependences.

The riskier area is no-growth append handling in `CodePreparation` and
`ScopBuilder`. This branch now records which successor of a recognized
no-growth comparison is the fast path instead of assuming `successor(0)` is
always the no-growth path. The same polarity information is used when
`ScopBuilder` turns a proven no-growth branch condition into a domain shortcut.

Validation performed on this branch:

```bash
python3 utils/check_stl_like_fusion.py \
  --strict \
  --example all \
  --opt /Users/mike/Coding/llvm-project/build-rv-polly/bin/opt \
  --clangxx /Users/mike/Coding/llvm-project/build-rv-polly/bin/clang++ \
  --target riscv64-unknown-elf \
  --sysroot /opt/homebrew/Cellar/riscv-gnu-toolchain/main/riscv64-unknown-elf \
  --gcc-toolchain /opt/homebrew/opt/riscv-gnu-toolchain
```

Observed result:

- `four_transform`: `PASS`
- `iota_transform_replace_copy`: `PASS`
- `three_transform`: `PASS`
- `pointer`: `PASS`

For the supported size-stable examples, the preserved harness artifacts also
confirm the expected IR transition:

- the input `<example>.ll` emitted by `clang++ -emit-llvm` does not contain
  Polly-generated blocks such as `polly.start` or `polly.stmt`
- the optimized `optimized.ll` emitted after
  `polly-prepare,scop(polly-opt-isl,polly-codegen),verify` contains the
  generated Polly path and passes LLVM IR verification

## Preliminary Host Benchmarking

The current host benchmark compares two versions of the same source-level
kernel:

- `no_polly.ll`: baseline LLVM optimization with no Polly-generated blocks
- `polly_optimized.ll`: Polly codegen path after offset-aware fusion, then the
  same backend optimization level

The benchmark harness is:

- [`utils/benchmark_stl_like_fusion.py`](../utils/benchmark_stl_like_fusion.py)

The host runs are intentionally local and mechanical, not a final performance
study. The default Polly codegen behavior still disables vectorization metadata
on fallback loops. This is useful as a conservative default, but it can poison
later cleanup/vectorization when the optimized path is simplified.

```bash
utils/benchmark_stl_like_fusion.py \
  --case all \
  --size 262144 \
  --repeats 9 \
  --warmups 2 \
  --allow-fallback-vectorization \
  --keep-dir <artifact-dir> \
  --opt /Users/mike/Coding/llvm-project/build-rv-polly/bin/opt \
  --clangxx /usr/bin/clang++
```

This confirmed the benchmarking setup:

- baseline IR does not contain Polly blocks
- Polly codegen IR does contain `polly.start` / `polly.stmt`
- schedule dumps still show the expected fused bands
- generated executables pass output-equivalence checks before timing

On a freshly rebuilt `opt` from this branch, the supported main-branch cases
are:

- `four_transform`
- `iota_transform_replace_copy`
- `three_transform`
- `pointer`

With default fallback-vectorization disabling, the Polly path is still much
slower because final loops carry `llvm.loop.vectorize.enable = false`. With
`--allow-fallback-vectorization`, the pure transform-style cases recover
backend vectorization and move close to parity:

- `four_transform`, `N = 262144`: `speedup = 0.8956`
- `iota_transform_replace_copy`, `N = 262144`: `speedup = 0.9784`
- `three_transform`, `N = 262144`: `speedup = 1.1213`
- `pointer`, `N = 262144`: `speedup = 1.0581`

The first diagnostic runs point to downstream codegen issues rather than to a
missing fusion signal:

- for a pure `three_transform` chain, Polly codegen leaves
  `llvm.loop.vectorize.enable = false` metadata on the final loops. With normal
  host backend codegen, the baseline becomes vectorized while the Polly path
  stays scalar. When backend vectorization is disabled for both sides, the two
  versions become structurally similar and the timing gap mostly disappears.
- copy/fill chains are intentionally not part of the current main-branch
  supported set yet. On a freshly rebuilt `opt` from this branch, examples such
  as `copy_transform_copy` do not form the desired 3-statement fused band; they
  need the later typed `memcpy`/`memmove`/`memset` expansion work that is still
  kept on the experimental branch.

The current main branch therefore has a first performance control:
`-polly-disable-fallback-vectorization=false`. This is not flipped as the
global default yet. A focused regression was added in
[`test/CodeGen/scev-backedgetaken.ll`](../test/CodeGen/scev-backedgetaken.ll):
the default path still checks for vectorization-disabling metadata, while the
new flag checks that this metadata is not emitted.

Full `check-polly` was also run on this branch after refreshing stale
regression expectations:

- `Passed`: 1061
- `Expectedly Failed`: 22
- `Unsupported`: 39
- real failures: 0

Some unrelated historical corner-case tests now explicitly check the
conservative outcome where Polly detects a candidate region but dismisses it
before code generation. That is a missed-optimization result, not a generated
miscompile path.

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
