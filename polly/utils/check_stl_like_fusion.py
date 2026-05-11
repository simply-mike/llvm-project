#!/usr/bin/env python3

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parent.parent
INPUTS = ROOT / "test" / "Inputs"


EXAMPLES = {
    "binary_transform": {
        "source": INPUTS / "stl_like_offset_binary_transform.cpp",
        "description": "Unary and binary std::transform passes with partial fusion",
        "require_offset": True,
        "require_compaction": False,
        "min_fused_stmts": 2,
        "require_codegen": True,
        "require_codegen_rtc": False,
    },
    "fill_transform_replace_copy": {
        "source": INPUTS / "stl_like_offset_fill_transform_replace_copy.cpp",
        "description": "std::fill + std::transform + std::replace_copy",
        "require_offset": True,
        "require_compaction": False,
        "min_fused_stmts": 3,
        "require_codegen": True,
        "require_codegen_rtc": False,
    },
    "four_transform": {
        "source": INPUTS / "stl_like_offset_four_transform.cpp",
        "description": "Four std::transform passes with small constant offsets",
        "require_offset": True,
        "require_compaction": False,
        "min_fused_stmts": 4,
        "require_codegen": True,
        "require_codegen_rtc": False,
    },
    "iota_transform_replace_copy": {
        "source": INPUTS / "stl_like_offset_iota_transform_replace_copy.cpp",
        "description": "std::iota + std::transform + std::replace_copy pipeline",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": True,
    },
    "for_each_transform": {
        "source": INPUTS / "stl_like_offset_for_each_transform.cpp",
        "description": "std::for_each + std::transform + std::transform",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": False,
    },
    "three_transform": {
        "source": INPUTS / "stl_like_offset_three_transform.cpp",
        "description": "Three std::transform passes with small constant offsets",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": False,
    },
    "pointer": {
        "source": INPUTS / "stl_like_offset_pointer.cpp",
        "description": "Standard-library algorithms over raw pointers",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 2,
        "require_codegen_rtc": False,
    },
    "replace_copy_if": {
        "source": INPUTS / "stl_like_offset_replace_copy_if.cpp",
        "description": "std::transform + std::replace_copy_if + std::transform",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": False,
    },
    "replace_if": {
        "source": INPUTS / "stl_like_offset_replace_if.cpp",
        "description": "std::transform + std::replace_if + std::transform",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": False,
    },
    "transform_replace_copy": {
        "source": INPUTS / "stl_like_offset_transform_replace_copy.cpp",
        "description": "std::transform + std::transform + std::replace_copy",
        "require_offset": True,
        "require_compaction": False,
        "require_codegen": True,
        "min_fused_stmts": 3,
        "require_codegen_rtc": False,
    },
    "vector": {
        "source": INPUTS / "stl_like_offset_vector.cpp",
        "description": "std::vector + std::back_inserter pipeline",
        "require_offset": False,
        "require_compaction": True,
        "min_fused_stmts": 4,
        "require_codegen_rtc": False,
    },
}

DEFAULT_EXAMPLES = [
    "binary_transform",
    "fill_transform_replace_copy",
    "for_each_transform",
    "four_transform",
    "iota_transform_replace_copy",
    "replace_copy_if",
    "replace_if",
    "three_transform",
    "transform_replace_copy",
    "pointer",
]

DEFAULT_CXXFLAGS = [
    "-O1",
    "-fno-vectorize",
    "-fno-slp-vectorize",
    "-fno-unroll-loops",
]


def run_cmd(cmd, cwd=None):
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        capture_output=True,
        check=False,
    )
    return proc.returncode, proc.stdout, proc.stderr


def count_max_fused_stmt_count(schedule_text):
    max_count = 0
    for line in schedule_text.splitlines():
        if "schedule:" not in line:
            continue
        max_count = max(max_count, line.count("Stmt"))
    return max_count


def detect_offset_evidence(scops_text, schedule_text, debug_text):
    return (
        "Logical Domain :=" in scops_text
        or "-> [(1 + i0)]" in schedule_text
        or "-> [(-1 + i0)]" in schedule_text
        or "Offset-aware fusion bonus" in debug_text
        or "Applying offset-aware outer schedule shift" in debug_text
    )


def detect_compaction_evidence(scops_text, debug_text):
    return (
        "copy-filter-like" in scops_text
        or "Recognized compaction-like statement" in debug_text
    )


def detect_codegen_rtc_evidence(optimized_ir_text):
    return (
        "%polly.rtc.result" in optimized_ir_text
        and "br i1 %polly.rtc.result" in optimized_ir_text
        and "br i1 false, label %polly.start" not in optimized_ir_text
    )


def compile_source(
    clangxx,
    source,
    ll_path,
    extra_cxxflags,
    target="",
    sysroot="",
    gcc_toolchain="",
):
    cmd = [
        clangxx,
        "-std=c++20",
        "-S",
        "-emit-llvm",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-discard-value-names",
    ]
    if target:
        cmd.append(f"--target={target}")
    if sysroot:
        cmd.append(f"--sysroot={sysroot}")
    if gcc_toolchain:
        cmd.append(f"--gcc-toolchain={gcc_toolchain}")
    cmd.extend(DEFAULT_CXXFLAGS)
    cmd.extend(extra_cxxflags)
    cmd.extend(
        [
        str(source),
        "-o",
        str(ll_path),
        ]
    )
    return run_cmd(cmd)


def run_polly(opt, plugin, ll_path, run_codegen):
    prefix = [opt]
    if plugin:
        prefix.extend(
            [
                "-load-pass-plugin",
                plugin,
            ]
        )

    common_flags = [
        "-polly-process-unprofitable",
        "-polly-allow-nonaffine",
        "-polly-force-offset-fusion=1",
        "-disable-output",
        str(ll_path),
    ]

    # Run polly-prepare explicitly in the integration harness so source-level
    # standard-library algorithm lowering exercises the same no-growth CFG
    # versioning path as the full optimization pipeline.
    scops_cmd = prefix + [
        "-passes=polly-prepare,print<polly-function-scops>",
        "-polly-detect-compaction-patterns",
    ] + common_flags

    schedule_cmd = prefix + [
        "-passes=polly-prepare,scop(polly-opt-isl,print<polly-opt-isl>)",
        "-polly-pattern-matching-based-opts=false",
        "-polly-postopts=0",
    ] + common_flags

    debug_cmd = prefix + [
        "-passes=polly-prepare,scop(polly-opt-isl)",
        "-polly-pattern-matching-based-opts=false",
        "-polly-postopts=0",
        "-debug-only=polly-opt-isl",
    ] + common_flags

    scops_rc, scops_out, scops_err = run_cmd(scops_cmd)
    sched_rc, sched_out, sched_err = run_cmd(schedule_cmd)
    dbg_rc, dbg_out, dbg_err = run_cmd(debug_cmd)
    optimized_text = ""
    codegen_rc, codegen_out, codegen_err = 0, "", ""
    if run_codegen:
        optimized_ll = ll_path.with_name("optimized.ll")
        codegen_cmd = prefix + [
            "-passes=polly-prepare,scop(polly-opt-isl,polly-codegen),verify",
            "-polly-pattern-matching-based-opts=false",
            "-polly-postopts=0",
            "-polly-process-unprofitable",
            "-polly-allow-nonaffine",
            "-polly-force-offset-fusion=1",
            "-S",
            str(ll_path),
            "-o",
            str(optimized_ll),
        ]
        codegen_rc, codegen_out, codegen_err = run_cmd(codegen_cmd)
        if optimized_ll.exists():
            optimized_text = optimized_ll.read_text(encoding="utf-8")

    return {
        "scops": (scops_rc, scops_out + scops_err),
        "schedule": (sched_rc, sched_out + sched_err),
        "debug": (dbg_rc, dbg_out + dbg_err),
        "codegen": (codegen_rc, codegen_out + codegen_err),
        "optimized_ir": optimized_text,
    }


def write_text(path, text):
    path.write_text(text, encoding="utf-8")


def evaluate(
    example_name,
    info,
    workdir,
    clangxx,
    opt,
    plugin,
    extra_cxxflags,
    strict,
    target,
    sysroot,
    gcc_toolchain,
):
    source = pathlib.Path(info["source"])
    ll_path = workdir / f"{example_name}.ll"

    rc, out, err = compile_source(
        clangxx,
        source,
        ll_path,
        extra_cxxflags,
        target=target,
        sysroot=sysroot,
        gcc_toolchain=gcc_toolchain,
    )
    if rc != 0:
        return {
            "name": example_name,
            "ok": False,
            "reason": "compile failed",
            "artifacts": {"compile.txt": out + err},
        }

    run_codegen = info.get("require_codegen", False) or info["require_codegen_rtc"]
    polly = run_polly(opt, plugin, ll_path, run_codegen)
    artifacts = {
        "compile.txt": out + err,
        "scops.txt": polly["scops"][1],
        "schedule.txt": polly["schedule"][1],
        "debug.txt": polly["debug"][1],
        "codegen.txt": polly["codegen"][1],
        "optimized.ll": polly["optimized_ir"],
    }

    offset_ok = detect_offset_evidence(
        artifacts["scops.txt"], artifacts["schedule.txt"], artifacts["debug.txt"]
    )
    compaction_ok = detect_compaction_evidence(
        artifacts["scops.txt"], artifacts["debug.txt"]
    )
    codegen_rtc_ok = detect_codegen_rtc_evidence(artifacts["optimized.ll"])
    fused_stmt_count = count_max_fused_stmt_count(artifacts["schedule.txt"])
    # Some toolchains build opt without debug-only support. Treat the debug
    # dump as best-effort and do not fail the whole integration check on it.
    rc_ok = all(polly[name][0] == 0 for name in ("scops", "schedule", "codegen"))
    has_signal = (
        fused_stmt_count >= info["min_fused_stmts"]
        or offset_ok
        or compaction_ok
        or codegen_rtc_ok
    )

    checks = []
    checks.append(rc_ok)
    if strict:
        if info["require_offset"]:
            checks.append(offset_ok)
        if info["require_compaction"]:
            checks.append(compaction_ok)
        if info["require_codegen_rtc"]:
            checks.append(codegen_rtc_ok)
        checks.append(fused_stmt_count >= info["min_fused_stmts"])
    else:
        checks.append(has_signal)

    reason = []
    reason.append(f"opt rc ok = {rc_ok}")
    reason.append(f"max fused stmt count = {fused_stmt_count}")
    reason.append(f"offset evidence = {offset_ok}")
    reason.append(f"compaction evidence = {compaction_ok}")
    reason.append(f"codegen rtc evidence = {codegen_rtc_ok}")
    reason.append(f"mode = {'strict' if strict else 'diagnostic'}")

    return {
        "name": example_name,
        "ok": all(checks),
        "reason": ", ".join(reason),
        "artifacts": artifacts,
        "ll_path": ll_path,
    }


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Best-effort integration check for Polly fusion on sequences of "
            "standard-library algorithms."
        )
    )
    parser.add_argument("--clangxx", default=shutil.which("clang++") or "clang++")
    parser.add_argument("--opt", default=shutil.which("opt") or "opt")
    parser.add_argument(
        "--plugin",
        default="",
        help="Path to LLVMPolly.so. Leave empty when opt already has Polly built in.",
    )
    parser.add_argument(
        "--target",
        default="",
        help="Cross-compilation target triple passed to clang++.",
    )
    parser.add_argument(
        "--sysroot",
        default="",
        help="Optional sysroot passed to clang++ for target headers and libc++.",
    )
    parser.add_argument(
        "--gcc-toolchain",
        default="",
        help="Optional GCC toolchain root passed to clang++ for cross headers/libs.",
    )
    parser.add_argument(
        "--example",
        choices=sorted(list(EXAMPLES.keys()) + ["all"]),
        default="all",
    )
    parser.add_argument("--keep-dir", default="")
    parser.add_argument(
        "--cxxflag",
        action="append",
        default=[],
        help="Extra flag passed to clang++.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Require the bundled example-specific Polly signals.",
    )
    args = parser.parse_args()

    selected = DEFAULT_EXAMPLES if args.example == "all" else [args.example]

    if args.keep_dir:
        workdir = pathlib.Path(args.keep_dir).resolve()
        workdir.mkdir(parents=True, exist_ok=True)
        cleanup = None
    else:
        cleanup = tempfile.TemporaryDirectory(prefix="polly-stl-like-")
        workdir = pathlib.Path(cleanup.name)

    results = []
    for name in selected:
        result = evaluate(
            name,
            EXAMPLES[name],
            workdir,
            args.clangxx,
            args.opt,
            args.plugin,
            args.cxxflag,
            args.strict,
            args.target,
            args.sysroot,
            args.gcc_toolchain,
        )
        results.append(result)

        example_dir = workdir / name
        example_dir.mkdir(parents=True, exist_ok=True)
        if "ll_path" in result:
            shutil.copy2(result["ll_path"], example_dir / result["ll_path"].name)
        for artifact_name, text in result["artifacts"].items():
            write_text(example_dir / artifact_name, text)

    print(f"Artifacts: {workdir}")
    all_ok = True
    for result in results:
        status = "PASS" if result["ok"] else "FAIL"
        print(f"[{status}] {result['name']}: {result['reason']}")
        all_ok &= result["ok"]

    if cleanup is not None and all_ok:
        print("Use --keep-dir <path> to preserve IR and Polly dumps.")

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
