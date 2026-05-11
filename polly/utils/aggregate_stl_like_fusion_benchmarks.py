#!/usr/bin/env python3

import argparse
import csv
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "docs" / "offset-aware-fusion-benchmark-grid.csv"


def read_benchmark_csv(path):
    lines = [
        line
        for line in path.read_text(encoding="utf-8").splitlines()
        if line and not line.startswith("Artifacts:")
    ]
    return list(csv.DictReader(lines))


def aggregate(paths, mode):
    rows_by_key = {}
    case_order = []
    size_order = []

    for path in paths:
        for row in read_benchmark_csv(path):
            case = row["case"]
            size = int(row["N"])
            if case not in case_order:
                case_order.append(case)
            if size not in size_order:
                size_order.append(size)
            rows_by_key.setdefault((case, size), []).append(row)

    def combine(values):
        if mode == "mean":
            return statistics.mean(values)
        return statistics.median(values)

    aggregated = []
    for case in case_order:
        for size in sorted(size_order):
            values = rows_by_key[(case, size)]
            baseline = round(combine([int(row["baseline_ns"]) for row in values]))
            polly = round(combine([int(row["polly_ns"]) for row in values]))
            speedup = combine([float(row["speedup"]) for row in values])
            aggregated.append(
                {
                    "case": case,
                    "N": size,
                    "baseline_ns": baseline,
                    "polly_ns": polly,
                    "speedup": f"{speedup:.4f}",
                    "max_fused_stmts": values[0]["max_fused_stmts"],
                    "baseline_has_polly": values[0]["baseline_has_polly"],
                    "polly_codegen_has_polly": values[0]["polly_codegen_has_polly"],
                }
            )

    return aggregated


def write_csv(path, rows):
    fieldnames = [
        "case",
        "N",
        "baseline_ns",
        "polly_ns",
        "speedup",
        "max_fused_stmts",
        "baseline_has_polly",
        "polly_codegen_has_polly",
    ]
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(
        description="Aggregate repeated size-stable Polly benchmark CSV outputs."
    )
    parser.add_argument("inputs", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument(
        "--mode",
        choices=("median", "mean"),
        default="median",
        help="Aggregation function for each (case, N) point.",
    )
    args = parser.parse_args()

    rows = aggregate(args.inputs, args.mode)
    write_csv(args.out, rows)
    print(f"Wrote {len(rows)} rows to {args.out} using {args.mode}")


if __name__ == "__main__":
    main()
