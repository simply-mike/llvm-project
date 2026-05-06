# Offset-Aware Fusion Status

This note tracks the current state of the Polly changes for automatic fusion of
pipelines lowered from standard-library algorithms whose iteration spaces differ
by small constant offsets. The primary validation target is RISC-V, while host
benchmarking is used as a practical performance sanity check.

## Problem Statement

Common source-level code often applies several standard-library algorithms to
the same contiguous storage, but over slightly different ranges:

- one pass uses `[0..N)`
- the next pass uses `[1..N)`
- another pass uses `[0..N-1)`

Examples include chains of `std::transform`, `std::iota`,
`std::replace_copy`, and similar size-stable algorithms over arrays or vectors.
These loops are close enough that fusion is often profitable, but baseline
Polly does not reliably fuse them once the statement domains stop matching
exactly.

The goal of this work is to support a useful limited class of these pipelines:

- recover small constant-offset relationships between adjacent statements
- let Polly's scheduler fuse statements whose domains are shifted by constants
- generate code where the hot interior loop survives later `O2` cleanup
- keep correctness checks live instead of disabling safety assumptions

This is intentionally not a general solution for every standard-library
algorithm.

## Supported Class

The currently supported stable class is:

- size-stable pipelines over contiguous storage
- affine or almost-affine loop domains
- domains that differ by small constant offsets such as `[0..N)`, `[1..N)`,
  `[0..N-1)`
- no data-dependent output growth in the stable end-to-end path

Confirmed examples:

- multiple `std::transform` passes, including 3-way and 4-way chains
- `std::iota` + `std::transform` + `std::replace_copy`
- pointer-style lowered examples that keep affine, size-stable behavior

Still outside the stable class:

- `std::copy_if(..., std::back_inserter(...))`
- true compaction, append, and grow patterns
- copy/fill chains that lower primarily to `memcpy`, `memmove`, or `memset`
  until typed intrinsic expansion is implemented

## Implemented Pieces

### Offset-Aware Scheduling

Implemented in:

- [`include/polly/Options.h`](../include/polly/Options.h)
- [`include/polly/Support/ISLTools.h`](../include/polly/Support/ISLTools.h)
- [`lib/Support/ISLTools.cpp`](../lib/Support/ISLTools.cpp)
- [`lib/Transform/ScheduleOptimizer.cpp`](../lib/Transform/ScheduleOptimizer.cpp)
- [`lib/Transform/ScheduleTreeTransform.cpp`](../lib/Transform/ScheduleTreeTransform.cpp)

Polly can now detect constant-offset relationships, add synthetic proximity
bonuses, and shift outer schedules before greedy fusion when
`-polly-force-offset-fusion=1` is enabled.

### Logical-Domain Recovery

Implemented in:

- [`include/polly/ScopBuilder.h`](../include/polly/ScopBuilder.h)
- [`include/polly/ScopInfo.h`](../include/polly/ScopInfo.h)
- [`include/polly/Support/ScopHelper.h`](../include/polly/Support/ScopHelper.h)
- [`lib/Analysis/ScopBuilder.cpp`](../lib/Analysis/ScopBuilder.cpp)
- [`lib/Analysis/ScopInfo.cpp`](../lib/Analysis/ScopInfo.cpp)
- [`lib/Analysis/ScopDetection.cpp`](../lib/Analysis/ScopDetection.cpp)
- [`lib/Support/ScopHelper.cpp`](../lib/Support/ScopHelper.cpp)

This recovers logical offset information from lowered access patterns and keeps
the original logical domain visible for diagnostics and scheduling.

### Interior-Loop Isolation

Implemented in:

- [`lib/Transform/ScheduleTreeTransform.cpp`](../lib/Transform/ScheduleTreeTransform.cpp)

After offset-aware fusion, Polly asks isl to isolate the common interior range
of the fused band. This peels small boundary slices such as `i = 0` or
`i = N-1` and leaves the hot fused loop without per-statement boundary guards.

The regression:

- [`test/ScheduleOptimizer/offset-aware-fusion.ll`](../test/ScheduleOptimizer/offset-aware-fusion.ll)

checks both:

- the fused schedule with an `isolate` AST option
- the generated AST shape with a clean interior loop

### Runtime-Check / APInt Fix

Implemented in:

- [`lib/Support/GICHelper.cpp`](../lib/Support/GICHelper.cpp)
- [`unittests/Isl/IslTest.cpp`](../unittests/Isl/IslTest.cpp)

The previous blocker was that an isl integer constant equal to `2^63` could be
converted into a 64-bit `APInt` that looked like signed `INT_MIN`. That made
runtime checks such as `v + 4*n >= 2^63` lower into comparisons that `O2`
simplified as always true, making the Polly branch unreachable.

The conversion now preserves a positive signed representation by widening such
values to `i65`. As a result, the Polly path survives `default<O2>` without
requiring the diagnostic `-polly-ignore-integer-wrapping` flag.

The regression:

- [`test/CodeGen/offset-aware-fusion-live-rtc.ll`](../test/CodeGen/offset-aware-fusion-live-rtc.ll)

checks that the generated code keeps a live `%polly.rtc.result` and a reachable
Polly path.

## Confirmed Working Cases

### Lowered IR

Important lowered-IR regressions:

- [`test/ScheduleOptimizer/offset-aware-fusion.ll`](../test/ScheduleOptimizer/offset-aware-fusion.ll)
- [`test/ScheduleOptimizer/offset-aware-compaction.ll`](../test/ScheduleOptimizer/offset-aware-compaction.ll)
- [`test/ScopInfo/compaction-pattern.ll`](../test/ScopInfo/compaction-pattern.ll)
- [`test/ScopInfo/inttoptr-phi-iterator.ll`](../test/ScopInfo/inttoptr-phi-iterator.ll)
- [`test/CodeGen/offset-aware-fusion-live-rtc.ll`](../test/CodeGen/offset-aware-fusion-live-rtc.ll)

### Source-Level Friendly Cases

Confirmed with the strict RISC-V harness:

- [`test/Inputs/stl_like_offset_four_transform.cpp`](../test/Inputs/stl_like_offset_four_transform.cpp)
  - `PASS`
  - fused band on 4 statements
- [`test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_three_transform.cpp`](../test/Inputs/stl_like_offset_three_transform.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_pointer.cpp`](../test/Inputs/stl_like_offset_pointer.cpp)
  - `PASS`
  - fused band on 2 statements in the stable check

The frontier vector/append example remains outside the supported class:

- [`test/Inputs/stl_like_offset_vector.cpp`](../test/Inputs/stl_like_offset_vector.cpp)

It is useful for research, but it should not yet be described as supported
`copy_if(back_inserter)` fusion.

## Verification

RISC-V source-level check:

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

Latest observed result:

- `four_transform`: `PASS`
- `iota_transform_replace_copy`: `PASS`
- `three_transform`: `PASS`
- `pointer`: `PASS`

Polly regression suite:

- `check-polly`
- `1061 passed`
- `22 expectedly failed`
- `39 unsupported`
- `0 failed`

Focused unit test:

- `IslTests`: `20 passed`

## Host Benchmarking

Benchmark harness:

- [`utils/benchmark_stl_like_fusion.py`](../utils/benchmark_stl_like_fusion.py)

The benchmark compares:

- `no_polly.ll`: baseline LLVM `default<O2>`
- `polly_optimized.ll`: Polly codegen after offset-aware fusion, then the same
  `default<O2>` pipeline

The harness also checks semantic equivalence before timing.

Current command shape:

```bash
python3 utils/benchmark_stl_like_fusion.py \
  --case all \
  --size 16384 \
  --size 262144 \
  --size 1048576 \
  --repeats 11 \
  --warmups 3 \
  --allow-fallback-vectorization \
  --diagnose \
  --keep-dir <artifact-dir> \
  --opt /Users/mike/Coding/llvm-project/build-rv-polly/bin/opt \
  --clangxx /usr/bin/clang++
```

With the APInt/RTC fix, this no longer needs `--ignore-integer-wrapping` for the
Polly path to survive final `O2`.

Latest local host results:

| Case | N=16384 | N=262144 | N=1048576 |
| ---- | ------- | -------- | --------- |
| `four_transform` | `1.8102x` | `1.7550x` | `2.3721x` |
| `iota_transform_replace_copy` | `1.6290x` | `1.7683x` | `1.7454x` |
| `three_transform` | `1.3333x` | `1.4055x` | `1.6844x` |
| `pointer` | `1.1188x` | `0.9406x` | `1.2287x` |

Diagnostic observations:

- `polly_codegen_ir` contains the generated Polly path.
- `polly_optimized_ir` still contains surviving `polly.*` blocks after
  `default<O2>`.
- The main transform-style examples now show real speedups without disabling
  integer-wrapping checks.
- The `pointer` case is still noisier and should not yet be used as the main
  performance claim.
- The benchmark executable checks baseline-vs-Polly output equivalence before
  collecting timings.

`--ignore-integer-wrapping` remains in the benchmark script only as a diagnostic
flag for isolating wrapping-check behavior. It is not required for the current
supported benchmark result.

### Generated IR Verification

After the benchmark run, the generated IR artifacts were verified explicitly:

```bash
for f in <artifact-dir>/*/{baseline_input,polly_input,no_polly,polly_codegen,polly_optimized}.ll; do
  /Users/mike/Coding/llvm-project/build-rv-polly/bin/opt \
    -passes=verify \
    -disable-output \
    "$f" || exit 1
done
```

Verified files:

- 4 cases
- 5 IR stages per case
- 20 LLVM IR files total

Result:

- verifier exit code: `0`
- expected local warning: the RISC-V Polly `opt` build may warn that it cannot
  create a target machine for host `arm64-apple` benchmark IR
- no verifier failures

## Current Status

Stable baseline:

- offset-aware fusion works for the selected size-stable class
- fused schedules are visible in `print<polly-opt-isl>`
- clean interior loops are visible in `print<polly-ast>`
- generated Polly paths survive final `O2`
- host benchmarks show real speedups on the main supported examples
- `check-polly` is green

Not stable yet:

- full `std::copy_if(..., std::back_inserter(...))`
- general compaction / append / grow semantics
- typed expansion for `memcpy` / `memmove` / `memset` into statement-level
  loops suitable for the same fusion path
- broad performance evaluation across machines, compilers, and larger kernels

## Next Plan

Short term:

- commit the current stable round as one cohesive patch-set
- refresh the benchmark documentation after one more repeated host run
- keep `--ignore-integer-wrapping` as a diagnostic-only option
- keep the explicit `i65` live-RTC codegen regression in place

Medium term:

- expand the supported size-stable class by patterns, not by individual
  examples
- add typed expansion for copy/fill-like intrinsics
- add more benchmark cases once those patterns are genuinely fused
- keep randomized differential testing for semantic safety

Later research frontier:

- return to `copy_if(back_inserter)` only after the size-stable class is clean
- model compaction as a distinct class, not as a shifted size-stable loop
- separate no-growth append fast paths from slow grow/reallocation paths in a
  path-sensitive way
- decide whether Polly should support such compaction through scheduler/codegen
  extensions or through a separate normalization pass before SCoP detection

## RISC-V Note

RISC-V validation needs a working sysroot:

- a RISC-V-capable LLVM/Polly build
- a usable RISC-V sysroot
- C++ standard library headers matching that sysroot

Bare-metal RISC-V is enough for the current friendly size-stable cases. Hosted
GNU/Linux-style RISC-V remains more representative for container-heavy cases
such as `std::vector` and `std::back_inserter`.

## Reproduction

Minimal RISC-V source-level check:

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

`--plugin` can be omitted in this setup because `build-rv-polly/bin/opt`
already contains Polly.

To preserve generated `*.ll`, `scops.txt`, `schedule.txt`, `debug.txt`, and
`optimized.ll`, pass `--keep-dir <path>` to the harness. These files are local
derived artifacts and are intentionally not committed.
