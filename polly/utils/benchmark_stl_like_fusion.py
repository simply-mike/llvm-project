#!/usr/bin/env python3

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
INPUTS = ROOT / "test" / "Inputs"


CASES = {
    "four_transform": {
        "source": INPUTS / "stl_like_offset_four_transform.cpp",
        "symbol": "stl_like_offset_four_transform",
        "kind": "two_buffer",
        "min_fused_stmts": 4,
    },
    "iota_transform_replace_copy": {
        "source": INPUTS / "stl_like_offset_iota_transform_replace_copy.cpp",
        "symbol": "stl_like_offset_iota_transform_replace_copy",
        "kind": "iota_two_buffer",
        "min_fused_stmts": 3,
    },
    "three_transform": {
        "source": INPUTS / "stl_like_offset_three_transform.cpp",
        "symbol": "stl_like_offset_three_transform",
        "kind": "two_buffer",
        "min_fused_stmts": 3,
    },
    "pointer": {
        "source": INPUTS / "stl_like_offset_pointer.cpp",
        "symbol": "stl_like_offset_pointer",
        "kind": "two_buffer",
        "min_fused_stmts": 2,
    },
}

FRONTEND_FLAGS = [
    "-std=c++20",
    "-O1",
    "-S",
    "-emit-llvm",
    "-fno-exceptions",
    "-fno-rtti",
    "-fno-discard-value-names",
    "-fno-vectorize",
    "-fno-slp-vectorize",
    "-fno-unroll-loops",
]

BASE_POLLY_FLAGS = [
    "-polly-process-unprofitable",
    "-polly-allow-nonaffine",
    "-polly-force-offset-fusion=1",
    "-polly-pattern-matching-based-opts=false",
    "-polly-postopts=0",
]


def run_cmd(cmd, cwd=None):
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        rendered = " ".join(str(part) for part in cmd)
        raise RuntimeError(
            f"command failed ({proc.returncode}): {rendered}\n"
            f"stdout:\n{proc.stdout}\n\nstderr:\n{proc.stderr}"
        )
    return proc.stdout + proc.stderr


def count_max_fused_stmt_count(schedule_text):
    return max(
        [line.count("Stmt") for line in schedule_text.splitlines() if "schedule:" in line]
        or [0]
    )


def has_polly_blocks(ir_text):
    return re.search(r"^polly[.$A-Za-z0-9_%-]*:", ir_text, re.MULTILINE) is not None


def count_ir_instructions(ir_text):
    count = 0
    for line in ir_text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith((";", "!", "attributes ")):
            continue
        if line.startswith("  ") and not stripped.endswith(":"):
            count += 1
    return count


def count_basic_blocks(ir_text):
    labels = 0
    for line in ir_text.splitlines():
        if re.match(r"^[A-Za-z0-9_.$%-]+:", line):
            labels += 1
    return labels


def summarize_ir(path):
    text = path.read_text(encoding="utf-8", errors="ignore")
    return {
        "ir_insts": count_ir_instructions(text),
        "basic_blocks": count_basic_blocks(text),
        "branches": len(re.findall(r"\bbr\b", text)),
        "cond_branches": text.count("br i1 "),
        "loads": len(re.findall(r"\bload\b", text)),
        "stores": len(re.findall(r"\bstore\b", text)),
        "calls": len(re.findall(r"\bcall\b", text)),
        "phis": len(re.findall(r"\bphi\b", text)),
        "icmps": len(re.findall(r"\bicmp\b", text)),
        "selects": len(re.findall(r"\bselect\b", text)),
        "vector_ops": len(re.findall(r"<\d+ x ", text)),
        "polly_blocks": len(re.findall(r"^polly[.$A-Za-z0-9_%-]*:", text, re.MULTILINE)),
        "has_polly": has_polly_blocks(text),
        "rtc_mentions": text.count("polly.rtc"),
        "loop_vectorize_disable": text.count('"llvm.loop.vectorize.enable", i32 0'),
    }


def summarize_asm(path):
    text = path.read_text(encoding="utf-8", errors="ignore")
    instructions = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", ";", ".", "//")):
            continue
        if stripped.endswith(":"):
            continue
        mnemonic = stripped.split()[0].rstrip(",")
        instructions.append(mnemonic)

    return {
        "asm_insts": len(instructions),
        "asm_branches": sum(
            1
            for mnemonic in instructions
            if mnemonic.startswith(("b", "j", "cb", "tb"))
        ),
        "asm_loads": sum(
            1 for mnemonic in instructions if mnemonic.startswith(("ldr", "ld", "mov"))
        ),
        "asm_stores": sum(1 for mnemonic in instructions if mnemonic.startswith(("str", "st"))),
        "asm_calls": sum(1 for mnemonic in instructions if mnemonic in ("bl", "call")),
        "asm_vector_mentions": len(re.findall(r"\b[vdq][0-9]+|\.[0-9]+[bhsd]\b", text)),
    }


def print_summary(prefix, summary):
    fields = ",".join(f"{key}={value}" for key, value in sorted(summary.items()))
    print(f"diagnostic,{prefix},{fields}")


def compile_to_ir(clangxx, source, symbol, renamed_symbol, output, cxxflags):
    cmd = [
        clangxx,
        *FRONTEND_FLAGS,
        f"-D{symbol}={renamed_symbol}",
        *cxxflags,
        str(source),
        "-o",
        str(output),
    ]
    run_cmd(cmd)


def optimize_baseline(opt, input_ll, output_ll):
    run_cmd([opt, "-passes=default<O2>", str(input_ll), "-S", "-o", str(output_ll)])


def polly_flags(args):
    flags = list(BASE_POLLY_FLAGS)
    if args.allow_fallback_vectorization:
        flags.append("-polly-disable-fallback-vectorization=false")
    if args.ignore_integer_wrapping:
        flags.append("-polly-ignore-integer-wrapping")
    return flags


def optimize_polly(args, input_ll, schedule_txt, polly_codegen_ll, polly_final_ll):
    flags = polly_flags(args)
    schedule = run_cmd(
        [
            args.opt,
            "-passes=polly-prepare,scop(polly-opt-isl,print<polly-opt-isl>)",
            *flags,
            "-disable-output",
            str(input_ll),
        ]
    )
    schedule_txt.write_text(schedule, encoding="utf-8")

    run_cmd(
        [
            args.opt,
            "-passes=polly-prepare,scop(polly-opt-isl,polly-codegen),verify",
            *flags,
            str(input_ll),
            "-S",
            "-o",
            str(polly_codegen_ll),
        ]
    )
    run_cmd(
        [
            args.opt,
            "-passes=default<O2>",
            str(polly_codegen_ll),
            "-S",
            "-o",
            str(polly_final_ll),
        ]
    )


def compile_object(clangxx, input_ll, output_obj, cxxflags):
    run_cmd(
        [
            clangxx,
            "-O2",
            *cxxflags,
            "-c",
            str(input_ll),
            "-o",
            str(output_obj),
        ]
    )


def compile_asm(clangxx, input_ll, output_asm, cxxflags):
    run_cmd(
        [
            clangxx,
            "-O2",
            *cxxflags,
            "-S",
            str(input_ll),
            "-o",
            str(output_asm),
        ]
    )


def driver_source(case):
    symbol = case["symbol"]
    kind = case["kind"]
    baseline = f"baseline_{symbol}"
    polly = f"polly_{symbol}"

    if kind == "copy_three_buffer":
        prototypes = f"""
extern "C" void {baseline}(const int *, int *, int *, std::size_t);
extern "C" void {polly}(const int *, int *, int *, std::size_t);
"""
        state = f"""
struct State {{
  std::vector<int> in;
  std::vector<int> tmp;
  std::vector<int> out;

  explicit State(std::size_t N) : in(N + 8), tmp(N + 8), out(N + 8) {{}}

  void reset() {{
    for (std::size_t I = 0; I < in.size(); ++I) {{
      in[I] = static_cast<int>((I * 17 + 3) % 1009);
      tmp[I] = -1000003;
      out[I] = -2000003;
    }}
  }}

  long long checksum() const {{
    long long S = 0;
    for (int X : in)
      S = S * 131 + X;
    for (int X : tmp)
      S = S * 131 + X;
    for (int X : out)
      S = S * 131 + X;
    return S;
  }}

  bool equals(const State &Other) const {{
    return in == Other.in && tmp == Other.tmp && out == Other.out;
  }}
}};

static void call_baseline(State &S, std::size_t N) {{
  {baseline}(S.in.data(), S.tmp.data(), S.out.data(), N);
}}

static void call_polly(State &S, std::size_t N) {{
  {polly}(S.in.data(), S.tmp.data(), S.out.data(), N);
}}
"""
    elif kind == "iota_two_buffer":
        prototypes = f"""
extern "C" void {baseline}(int *, int *, std::size_t, int);
extern "C" void {polly}(int *, int *, std::size_t, int);
"""
        state = f"""
struct State {{
  std::vector<int> tmp;
  std::vector<int> out;

  explicit State(std::size_t N) : tmp(N + 8), out(N + 8) {{}}

  void reset() {{
    for (std::size_t I = 0; I < tmp.size(); ++I) {{
      tmp[I] = -1000003;
      out[I] = -2000003;
    }}
  }}

  long long checksum() const {{
    long long S = 0;
    for (int X : tmp)
      S = S * 131 + X;
    for (int X : out)
      S = S * 131 + X;
    return S;
  }}

  bool equals(const State &Other) const {{
    return tmp == Other.tmp && out == Other.out;
  }}
}};

static void call_baseline(State &S, std::size_t N) {{
  {baseline}(S.tmp.data(), S.out.data(), N, 13);
}}

static void call_polly(State &S, std::size_t N) {{
  {polly}(S.tmp.data(), S.out.data(), N, 13);
}}
"""
    else:
        prototypes = f"""
extern "C" void {baseline}(int *, int *, std::size_t);
extern "C" void {polly}(int *, int *, std::size_t);
"""
        state = f"""
struct State {{
  std::vector<int> a;
  std::vector<int> b;

  explicit State(std::size_t N) : a(N + 8), b(N + 8) {{}}

  void reset() {{
    for (std::size_t I = 0; I < a.size(); ++I) {{
      a[I] = static_cast<int>((I * 17 + 3) % 1009);
      b[I] = -2000003;
    }}
  }}

  long long checksum() const {{
    long long S = 0;
    for (int X : a)
      S = S * 131 + X;
    for (int X : b)
      S = S * 131 + X;
    return S;
  }}

  bool equals(const State &Other) const {{
    return a == Other.a && b == Other.b;
  }}
}};

static void call_baseline(State &S, std::size_t N) {{
  {baseline}(S.a.data(), S.b.data(), N);
}}

static void call_polly(State &S, std::size_t N) {{
  {polly}(S.a.data(), S.b.data(), N);
}}
"""

    return f"""
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

{prototypes}

volatile long long Sink;

{state}

static long long median(std::vector<long long> Values) {{
  std::sort(Values.begin(), Values.end());
  return Values[Values.size() / 2];
}}

template <typename Fn>
static long long timed_call(State &S, std::size_t N, Fn F) {{
  S.reset();
  auto Start = std::chrono::steady_clock::now();
  F(S, N);
  auto End = std::chrono::steady_clock::now();
  Sink ^= S.checksum();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(End - Start).count();
}}

int main(int argc, char **argv) {{
  if (argc != 4) {{
    std::cerr << "usage: " << argv[0] << " <N> <repeats> <warmups>\\n";
    return 2;
  }}

  std::size_t N = std::strtoull(argv[1], nullptr, 10);
  int Repeats = std::atoi(argv[2]);
  int Warmups = std::atoi(argv[3]);
  if (N <= 1 || Repeats <= 0 || Warmups < 0)
    return 2;

  State A(N), B(N);
  A.reset();
  B.reset();
  call_baseline(A, N);
  call_polly(B, N);
  if (!A.equals(B)) {{
    std::cerr << "mismatch\\n";
    return 1;
  }}

  for (int I = 0; I < Warmups; ++I) {{
    State Warm(N);
    if (I % 2 == 0)
      timed_call(Warm, N, call_baseline);
    else
      timed_call(Warm, N, call_polly);
  }}

  std::vector<long long> BaselineTimes;
  std::vector<long long> PollyTimes;
  BaselineTimes.reserve(Repeats);
  PollyTimes.reserve(Repeats);

  for (int I = 0; I < Repeats; ++I) {{
    State First(N), Second(N);
    if (I % 2 == 0) {{
      BaselineTimes.push_back(timed_call(First, N, call_baseline));
      PollyTimes.push_back(timed_call(Second, N, call_polly));
    }} else {{
      PollyTimes.push_back(timed_call(First, N, call_polly));
      BaselineTimes.push_back(timed_call(Second, N, call_baseline));
    }}
  }}

  auto BaselineMedian = median(BaselineTimes);
  auto PollyMedian = median(PollyTimes);
  double Speedup = static_cast<double>(BaselineMedian) /
                   static_cast<double>(PollyMedian);

  std::cout << "baseline_ns=" << BaselineMedian
            << " polly_ns=" << PollyMedian
            << " speedup=" << Speedup
            << " checksum=" << Sink << "\\n";
  return 0;
}}
"""


def build_case(args, case_name, case, root):
    case_dir = root / case_name
    case_dir.mkdir(parents=True, exist_ok=True)

    baseline_input = case_dir / "baseline_input.ll"
    polly_input = case_dir / "polly_input.ll"
    baseline_ll = case_dir / "no_polly.ll"
    polly_codegen_ll = case_dir / "polly_codegen.ll"
    polly_final_ll = case_dir / "polly_optimized.ll"
    schedule_txt = case_dir / "schedule.txt"
    baseline_obj = case_dir / "baseline.o"
    polly_obj = case_dir / "polly.o"
    baseline_asm = case_dir / "baseline.s"
    polly_asm = case_dir / "polly.s"
    driver_cpp = case_dir / "driver.cpp"
    driver_obj = case_dir / "driver.o"
    exe = case_dir / "bench"

    compile_to_ir(
        args.clangxx,
        case["source"],
        case["symbol"],
        f"baseline_{case['symbol']}",
        baseline_input,
        args.cxxflag,
    )
    compile_to_ir(
        args.clangxx,
        case["source"],
        case["symbol"],
        f"polly_{case['symbol']}",
        polly_input,
        args.cxxflag,
    )
    optimize_baseline(args.opt, baseline_input, baseline_ll)
    optimize_polly(args, polly_input, schedule_txt, polly_codegen_ll, polly_final_ll)

    compile_object(args.clangxx, baseline_ll, baseline_obj, args.cxxflag)
    compile_object(args.clangxx, polly_final_ll, polly_obj, args.cxxflag)
    compile_asm(args.clangxx, baseline_ll, baseline_asm, args.cxxflag)
    compile_asm(args.clangxx, polly_final_ll, polly_asm, args.cxxflag)

    driver_cpp.write_text(driver_source(case), encoding="utf-8")
    run_cmd(
        [
            args.clangxx,
            "-std=c++20",
            "-O2",
            *args.cxxflag,
            "-c",
            str(driver_cpp),
            "-o",
            str(driver_obj),
        ]
    )
    run_cmd(
        [
            args.clangxx,
            "-O2",
            *args.cxxflag,
            str(driver_obj),
            str(baseline_obj),
            str(polly_obj),
            "-o",
            str(exe),
        ]
    )

    baseline_input_has_polly = has_polly_blocks(
        baseline_input.read_text(encoding="utf-8", errors="ignore")
    )
    baseline_has_polly = has_polly_blocks(
        baseline_ll.read_text(encoding="utf-8", errors="ignore")
    )
    polly_codegen_has_polly = has_polly_blocks(
        polly_codegen_ll.read_text(encoding="utf-8", errors="ignore")
    )
    max_fused = count_max_fused_stmt_count(schedule_txt.read_text(encoding="utf-8"))

    if baseline_input_has_polly or baseline_has_polly:
        raise RuntimeError(f"{case_name}: baseline IR unexpectedly contains Polly blocks")
    if not polly_codegen_has_polly:
        raise RuntimeError(f"{case_name}: Polly codegen IR does not contain Polly blocks")
    if max_fused < case["min_fused_stmts"]:
        raise RuntimeError(
            f"{case_name}: expected at least {case['min_fused_stmts']} fused "
            f"statements, got {max_fused}"
        )

    return {
        "exe": exe,
        "max_fused": max_fused,
        "baseline_input_has_polly": baseline_input_has_polly,
        "baseline_has_polly": baseline_has_polly,
        "polly_codegen_has_polly": polly_codegen_has_polly,
        "diagnostics": {
            "baseline_input_ir": summarize_ir(baseline_input),
            "no_polly_ir": summarize_ir(baseline_ll),
            "polly_codegen_ir": summarize_ir(polly_codegen_ll),
            "polly_optimized_ir": summarize_ir(polly_final_ll),
            "baseline_asm": summarize_asm(baseline_asm),
            "polly_asm": summarize_asm(polly_asm),
        },
    }


def run_benchmark(exe, size, repeats, warmups):
    output = run_cmd([str(exe), str(size), str(repeats), str(warmups)])
    match = re.search(
        r"baseline_ns=(\d+) polly_ns=(\d+) speedup=([0-9.]+) checksum=(-?\d+)",
        output,
    )
    if not match:
        raise RuntimeError(f"could not parse benchmark output:\n{output}")
    return {
        "baseline_ns": int(match.group(1)),
        "polly_ns": int(match.group(2)),
        "speedup": float(match.group(3)),
        "checksum": int(match.group(4)),
    }


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Build and benchmark size-stable standard-library fusion examples "
            "with and without Polly code generation."
        )
    )
    parser.add_argument("--clangxx", default=shutil.which("clang++") or "clang++")
    parser.add_argument("--opt", default=shutil.which("opt") or "opt")
    parser.add_argument(
        "--case",
        choices=sorted(list(CASES.keys()) + ["all"]),
        default="all",
    )
    parser.add_argument(
        "--size",
        action="append",
        type=int,
        default=[],
        help="Problem size N. Can be passed more than once.",
    )
    parser.add_argument("--repeats", type=int, default=15)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument(
        "--diagnose",
        action="store_true",
        help="Print structural IR/assembly summaries for each built case.",
    )
    parser.add_argument(
        "--allow-fallback-vectorization",
        action="store_true",
        help=(
            "Pass -polly-disable-fallback-vectorization=false when running "
            "Polly codegen. This helps diagnose whether fallback loop metadata "
            "prevents later backend vectorization."
        ),
    )
    parser.add_argument(
        "--ignore-integer-wrapping",
        action="store_true",
        help=(
            "Pass -polly-ignore-integer-wrapping when running Polly. This is "
            "useful for diagnosing whether wrapping assumptions make the "
            "runtime check fold to the fallback path before benchmarking."
        ),
    )
    parser.add_argument("--keep-dir", default="")
    parser.add_argument(
        "--cxxflag",
        action="append",
        default=[],
        help="Extra flag passed to clang++ for all compile/link steps.",
    )
    args = parser.parse_args()

    sizes = args.size or [1024, 16384, 262144]
    selected = CASES.keys() if args.case == "all" else [args.case]

    if args.keep_dir:
        workroot = pathlib.Path(args.keep_dir).resolve()
        workroot.mkdir(parents=True, exist_ok=True)
        cleanup = None
    else:
        cleanup = tempfile.TemporaryDirectory(prefix="polly-stl-bench-")
        workroot = pathlib.Path(cleanup.name)

    print(f"Artifacts: {workroot}")
    print(
        "case,N,baseline_ns,polly_ns,speedup,max_fused_stmts,"
        "baseline_has_polly,polly_codegen_has_polly"
    )

    for case_name in selected:
        build = build_case(args, case_name, CASES[case_name], workroot)
        if args.diagnose:
            for name, summary in build["diagnostics"].items():
                print_summary(f"{case_name},{name}", summary)
        for size in sizes:
            result = run_benchmark(build["exe"], size, args.repeats, args.warmups)
            print(
                f"{case_name},{size},{result['baseline_ns']},{result['polly_ns']},"
                f"{result['speedup']:.4f},{build['max_fused']},"
                f"{build['baseline_has_polly']},{build['polly_codegen_has_polly']}"
            )

    if cleanup is not None:
        print("Use --keep-dir <path> to preserve benchmark artifacts.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
