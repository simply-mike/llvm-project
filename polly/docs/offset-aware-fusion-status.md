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
- `std::fill` + `std::transform` + `std::replace_copy` on RISC-V source
  validation
- `std::for_each` + `std::transform` + `std::transform`
- `std::iota` + `std::transform` + `std::replace_copy`
- `std::transform` + `std::replace_copy_if` + `std::transform`
- `std::transform` + `std::replace_if` + `std::transform`
- `std::transform` + `std::transform` + `std::replace_copy`
- pointer-style lowered examples that keep affine, size-stable behavior
- a mixed unary/binary `std::transform` chain with partial fused-band coverage

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

- [`test/Inputs/stl_like_offset_binary_transform.cpp`](../test/Inputs/stl_like_offset_binary_transform.cpp)
  - `PASS`
  - partial fused band on 2 statements
- [`test/Inputs/stl_like_offset_fill_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_fill_transform_replace_copy.cpp)
  - `PASS`
  - fused band on 3 statements in the RISC-V source-level check
- [`test/Inputs/stl_like_offset_for_each_transform.cpp`](../test/Inputs/stl_like_offset_for_each_transform.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_four_transform.cpp`](../test/Inputs/stl_like_offset_four_transform.cpp)
  - `PASS`
  - fused band on 4 statements
- [`test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_iota_transform_replace_copy.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_replace_copy_if.cpp`](../test/Inputs/stl_like_offset_replace_copy_if.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_replace_if.cpp`](../test/Inputs/stl_like_offset_replace_if.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_three_transform.cpp`](../test/Inputs/stl_like_offset_three_transform.cpp)
  - `PASS`
  - fused band on 3 statements
- [`test/Inputs/stl_like_offset_transform_replace_copy.cpp`](../test/Inputs/stl_like_offset_transform_replace_copy.cpp)
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

- `binary_transform`: `PASS`
- `fill_transform_replace_copy`: `PASS`
- `for_each_transform`: `PASS`
- `four_transform`: `PASS`
- `iota_transform_replace_copy`: `PASS`
- `replace_copy_if`: `PASS`
- `replace_if`: `PASS`
- `three_transform`: `PASS`
- `transform_replace_copy`: `PASS`
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
for run in 1 2 3 4 5; do
  python3 utils/benchmark_stl_like_fusion.py \
    --case all \
    --size 4096 --size 8192 --size 16384 --size 32768 \
    --size 65536 --size 131072 --size 262144 \
    --size 524288 --size 1048576 \
    --repeats 21 \
    --warmups 5 \
    --allow-fallback-vectorization \
    --keep-dir <artifact-dir>/run-${run} \
    --opt /Users/mike/Coding/llvm-project/build-rv-polly/bin/opt \
    --clangxx /usr/bin/clang++ \
    --frontend-clangxx /Users/mike/Coding/llvm-project/build-rv-polly/bin/clang++ \
    --native-clangxx /usr/bin/clang++ \
    --cxxflag=--target=arm64-apple-macosx26.0.0 \
    --cxxflag=-isysroot \
    --cxxflag=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
done
```

The committed CSV uses the median value across those five independent grid
runs for each `(case, N)` point.

Those repeated outputs can be aggregated with:

```bash
python3 utils/aggregate_stl_like_fusion_benchmarks.py \
  <artifact-dir>/run-1.csv \
  <artifact-dir>/run-2.csv \
  <artifact-dir>/run-3.csv \
  <artifact-dir>/run-4.csv \
  <artifact-dir>/run-5.csv \
  --mode median \
  --out docs/offset-aware-fusion-benchmark-grid.csv
```

The latest grid data for plotting is stored in:

- [`docs/offset-aware-fusion-benchmark-grid.csv`](offset-aware-fusion-benchmark-grid.csv)

Generated benchmark plots:

- [`docs/offset-aware-fusion-speedup-lines.svg`](offset-aware-fusion-speedup-lines.svg)
- [`docs/offset-aware-fusion-speedup-summary.svg`](offset-aware-fusion-speedup-summary.svg)
- [`docs/offset-aware-fusion-speedup-heatmap.svg`](offset-aware-fusion-speedup-heatmap.svg)

They can be regenerated with:

```bash
python3 utils/plot_stl_like_fusion_benchmarks.py \
  --csv docs/offset-aware-fusion-benchmark-grid.csv \
  --out-dir docs
```

The benchmark now separates source lowering from native execution. The C++
source-to-LLVM-IR frontend is the locally built `build-rv-polly/bin/clang++`,
and Polly itself is run through the locally built `build-rv-polly/bin/opt`.
Native object generation, assembly dumps, driver compilation, and linking still
use `/usr/bin/clang++` because the current local LLVM build has no default
native target backend (`clang++ --version` reports `Target: unknown`).

The benchmark frontend intentionally uses `-fno-builtin` together with the other
loop-preserving frontend flags. This prevents `std::fill` from becoming
`llvm.experimental.memset.pattern` before Polly sees the code. That keeps the
benchmark focused on the current typed-loop size-stable class; intrinsic
expansion remains a separate future task.

With the APInt/RTC fix, this no longer needs `--ignore-integer-wrapping` for the
Polly path to survive final `O2`.

Latest local host benchmark results:

| Case | N=16384 | N=262144 | N=1048576 |
| ---- | ------- | -------- | --------- |
| `binary_transform` | `1.4723x` | `1.3837x` | `1.3674x` |
| `fill_transform_replace_copy` | `1.3904x` | `1.2890x` | `1.2481x` |
| `four_transform` | `1.7569x` | `1.7143x` | `2.1616x` |
| `for_each_transform` | `1.3370x` | `1.3239x` | `1.5524x` |
| `iota_transform_replace_copy` | `1.6263x` | `1.6789x` | `1.3780x` |
| `replace_copy_if` | `1.5418x` | `1.3327x` | `1.6088x` |
| `replace_if` | `2.2683x` | `2.0769x` | `2.1553x` |
| `three_transform` | `1.3332x` | `1.3279x` | `1.4075x` |
| `transform_replace_copy` | `1.3584x` | `1.3126x` | `1.4688x` |
| `pointer` | `1.3823x` | `1.3379x` | `1.3419x` |

Diagnostic observations:

- `polly_codegen_ir` contains the generated Polly path.
- `polly_optimized_ir` still contains surviving `polly.*` blocks after
  `default<O2>`.
- The expanded size-stable examples show real speedups in this local smoke run
  without disabling integer-wrapping checks.
- The main transform-style examples now show real speedups without disabling
  integer-wrapping checks.
- The `pointer` case is still noisier and should not yet be used as the main
  performance claim.
- The `binary_transform` case currently demonstrates partial mixed-chain fusion,
  not full 3-way fusion of the whole sequence.
- Without `-fno-builtin`, Apple host lowering turns the `std::fill` part of
  `fill_transform_replace_copy` into `llvm.experimental.memset.pattern`, so
  that case leaves the current typed-loop class before Polly can fuse it.
- The benchmark executable checks baseline-vs-Polly output equivalence before
  collecting timings.
- The previously suspicious `four_transform` spike at `N=16384` and the
  `replace_if` dip around `N=32768` disappeared after aggregating five
  independent grid runs. The median values are `1.7569x` and `2.1660x`,
  respectively.
- The speedup curve is not strictly monotonic. Some cases lose relative speedup
  around larger `N` because execution becomes more memory-bandwidth-sensitive
  and the Polly path still carries RTC/fallback/boundary code around the hot
  fused loop. Other cases improve at larger `N` when reducing memory passes
  dominates this scaffolding overhead.

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

- 10 host-benchmarkable cases
- 5 IR stages per case
- 50 LLVM IR files total

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
