#!/usr/bin/env python3

import argparse
import csv
import math
import statistics
from html import escape
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CSV = ROOT / "docs" / "offset-aware-fusion-benchmark-grid.csv"
DEFAULT_OUT_DIR = ROOT / "docs"


COLORS = [
    "#1b4d89",
    "#c44536",
    "#2d936c",
    "#f2a541",
    "#6b4e9b",
    "#0087a7",
    "#9a6b1f",
    "#d1498c",
    "#4f6f52",
    "#7c3f58",
]


def read_grid(path):
    rows = list(csv.DictReader(path.open(encoding="utf-8")))
    case_order = []
    for row in rows:
        if row["case"] not in case_order:
            case_order.append(row["case"])

    sizes = sorted({int(row["N"]) for row in rows})
    data = {case: {} for case in case_order}
    for row in rows:
        data[row["case"]][int(row["N"])] = float(row["speedup"])

    return case_order, sizes, data


def svg_header(width, height):
    return [
        (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" '
            f'height="{height}" viewBox="0 0 {width} {height}">'
        ),
        '<rect width="100%" height="100%" fill="#fbfaf7"/>',
        (
            "<style>"
            "text{font-family:Hoefler Text, Georgia, serif; fill:#24313f}"
            ".label{font-size:13px}"
            ".small{font-size:11px; fill:#53616f}"
            ".title{font-size:24px; font-weight:700}"
            ".subtitle{font-size:13px; fill:#53616f}"
            ".axis{stroke:#7c8794; stroke-width:1}"
            ".grid{stroke:#d8ddd8; stroke-width:1}"
            ".legend{font-size:11px}"
            "</style>"
        ),
    ]


def save_svg(path, parts):
    path.write_text("\n".join(parts + ["</svg>"]) + "\n", encoding="utf-8")


def size_label(size):
    return f"{size // 1024}K" if size < 1048576 else "1M"


def x_pos(size, sizes, left, width):
    lo, hi = math.log2(sizes[0]), math.log2(sizes[-1])
    return left + (math.log2(size) - lo) / (hi - lo) * width


def y_pos(value, top, height, ymin=1.0, ymax=4.0):
    return top + (ymax - value) / (ymax - ymin) * height


def color_for(value):
    # Interpolate from warm low values to green high values in [1.0, 3.8].
    t = max(0, min(1, (value - 1.0) / (3.8 - 1.0)))
    r1, g1, b1 = (244, 222, 179)
    r2, g2, b2 = (21, 111, 78)
    r = round(r1 + (r2 - r1) * t)
    g = round(g1 + (g2 - g1) * t)
    b = round(b1 + (b2 - b1) * t)
    return f"#{r:02x}{g:02x}{b:02x}"


def plot_lines(out_dir, case_order, sizes, data):
    width, height = 1280, 760
    left, right, top, bottom = 95, 300, 95, 105
    plot_width = width - left - right
    plot_height = height - top - bottom
    case_colors = {case: COLORS[i % len(COLORS)] for i, case in enumerate(case_order)}

    parts = svg_header(width, height)
    parts.append(
        '<text class="title" x="95" y="48">'
        "Offset-aware fusion benchmark: speedup vs problem size</text>"
    )
    parts.append(
        '<text class="subtitle" x="95" y="72">'
        "Higher is better. X axis is log2(N). Data from benchmark grid CSV."
        "</text>"
    )

    for y_value in [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]:
        y = y_pos(y_value, top, plot_height)
        parts.append(
            f'<line class="grid" x1="{left}" y1="{y:.1f}" '
            f'x2="{left + plot_width}" y2="{y:.1f}"/>'
        )
        parts.append(f'<text class="small" x="{left - 42}" y="{y + 4:.1f}">{y_value:.1f}x</text>')

    for size in sizes:
        x = x_pos(size, sizes, left, plot_width)
        parts.append(
            f'<line class="grid" x1="{x:.1f}" y1="{top}" '
            f'x2="{x:.1f}" y2="{top + plot_height}"/>'
        )
        parts.append(
            f'<text class="small" text-anchor="middle" x="{x:.1f}" '
            f'y="{top + plot_height + 28}">{size_label(size)}</text>'
        )

    parts.append(
        f'<line class="axis" x1="{left}" y1="{top + plot_height}" '
        f'x2="{left + plot_width}" y2="{top + plot_height}"/>'
    )
    parts.append(
        f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" '
        f'y2="{top + plot_height}"/>'
    )
    parts.append(
        f'<text class="label" text-anchor="middle" x="{left + plot_width / 2}" '
        f'y="{height - 32}">Problem size N</text>'
    )
    parts.append(
        f'<text class="label" transform="translate(28 {top + plot_height / 2}) '
        f'rotate(-90)" text-anchor="middle">Speedup baseline / Polly</text>'
    )

    for case in case_order:
        points = [
            f"{x_pos(size, sizes, left, plot_width):.1f},"
            f"{y_pos(data[case][size], top, plot_height):.1f}"
            for size in sizes
        ]
        color = case_colors[case]
        parts.append(
            f'<polyline fill="none" stroke="{color}" stroke-width="2.4" '
            f'stroke-linejoin="round" stroke-linecap="round" '
            f'points="{" ".join(points)}"/>'
        )
        for size in sizes:
            parts.append(
                f'<circle cx="{x_pos(size, sizes, left, plot_width):.1f}" '
                f'cy="{y_pos(data[case][size], top, plot_height):.1f}" '
                f'r="3" fill="{color}"/>'
            )

    legend_x, legend_y = left + plot_width + 35, top + 8
    for idx, case in enumerate(case_order):
        y = legend_y + idx * 27
        parts.append(
            f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 24}" '
            f'y2="{y}" stroke="{case_colors[case]}" stroke-width="3"/>'
        )
        parts.append(f'<text class="legend" x="{legend_x + 34}" y="{y + 4}">{escape(case)}</text>')

    save_svg(out_dir / "offset-aware-fusion-speedup-lines.svg", parts)


def plot_summary(out_dir, case_order, sizes, data):
    width, height = 1120, 680
    left, right, top, bottom = 95, 70, 95, 95
    plot_width = width - left - right
    plot_height = height - top - bottom

    parts = svg_header(width, height)
    parts.append('<text class="title" x="95" y="48">Speedup distribution across supported examples</text>')
    parts.append('<text class="subtitle" x="95" y="72">Median line with min-max vertical bars for each N.</text>')

    for y_value in [1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0]:
        y = y_pos(y_value, top, plot_height)
        parts.append(
            f'<line class="grid" x1="{left}" y1="{y:.1f}" '
            f'x2="{left + plot_width}" y2="{y:.1f}"/>'
        )
        parts.append(f'<text class="small" x="{left - 42}" y="{y + 4:.1f}">{y_value:.1f}x</text>')

    median_points = []
    for size in sizes:
        values = [data[case][size] for case in case_order]
        min_value = min(values)
        median_value = statistics.median(values)
        max_value = max(values)
        x = x_pos(size, sizes, left, plot_width)
        y_min = y_pos(min_value, top, plot_height)
        y_median = y_pos(median_value, top, plot_height)
        y_max = y_pos(max_value, top, plot_height)
        parts.append(f'<line x1="{x:.1f}" y1="{y_min:.1f}" x2="{x:.1f}" y2="{y_max:.1f}" stroke="#74808b" stroke-width="2"/>')
        parts.append(f'<line x1="{x - 8:.1f}" y1="{y_min:.1f}" x2="{x + 8:.1f}" y2="{y_min:.1f}" stroke="#74808b" stroke-width="2"/>')
        parts.append(f'<line x1="{x - 8:.1f}" y1="{y_max:.1f}" x2="{x + 8:.1f}" y2="{y_max:.1f}" stroke="#74808b" stroke-width="2"/>')
        parts.append(f'<circle cx="{x:.1f}" cy="{y_median:.1f}" r="5" fill="#b23a48"/>')
        median_points.append(f"{x:.1f},{y_median:.1f}")
        parts.append(
            f'<text class="small" text-anchor="middle" x="{x:.1f}" '
            f'y="{top + plot_height + 28}">{size_label(size)}</text>'
        )

    parts.append(f'<polyline fill="none" stroke="#b23a48" stroke-width="3" points="{" ".join(median_points)}"/>')
    parts.append(
        f'<line class="axis" x1="{left}" y1="{top + plot_height}" '
        f'x2="{left + plot_width}" y2="{top + plot_height}"/>'
    )
    parts.append(
        f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" '
        f'y2="{top + plot_height}"/>'
    )
    parts.append(
        f'<text class="label" text-anchor="middle" x="{left + plot_width / 2}" '
        f'y="{height - 28}">Problem size N</text>'
    )
    parts.append(
        f'<text class="label" transform="translate(28 {top + plot_height / 2}) '
        f'rotate(-90)" text-anchor="middle">Speedup</text>'
    )
    parts.append(
        f'<text class="small" x="{left + plot_width - 190}" y="{top + 20}">'
        '<tspan fill="#b23a48">●</tspan> median, gray bars min-max</text>'
    )

    save_svg(out_dir / "offset-aware-fusion-speedup-summary.svg", parts)


def plot_heatmap(out_dir, case_order, sizes, data):
    width, height = 1220, 650
    left, top = 245, 95
    cell_width, cell_height = 90, 42

    parts = svg_header(width, height)
    parts.append('<text class="title" x="80" y="48">Speedup heatmap</text>')
    parts.append('<text class="subtitle" x="80" y="72">Darker green means larger baseline/Polly speedup.</text>')

    for idx, size in enumerate(sizes):
        x = left + idx * cell_width + cell_width / 2
        parts.append(
            f'<text class="small" text-anchor="middle" x="{x:.1f}" '
            f'y="{top - 20}">{size_label(size)}</text>'
        )

    for row_idx, case in enumerate(case_order):
        y = top + row_idx * cell_height
        parts.append(f'<text class="small" text-anchor="end" x="{left - 12}" y="{y + 26}">{escape(case)}</text>')
        for col_idx, size in enumerate(sizes):
            value = data[case][size]
            x = left + col_idx * cell_width
            text_color = "#ffffff" if value >= 2.0 else "#24313f"
            parts.append(
                f'<rect x="{x}" y="{y}" width="{cell_width - 2}" '
                f'height="{cell_height - 2}" rx="7" fill="{color_for(value)}"/>'
            )
            parts.append(
                f'<text text-anchor="middle" x="{x + cell_width / 2 - 1:.1f}" '
                f'y="{y + 26}" font-size="12" fill="{text_color}" '
                f'font-family="Hoefler Text, Georgia, serif">{value:.2f}x</text>'
            )

    parts.append(
        f'<text class="label" text-anchor="middle" '
        f'x="{left + len(sizes) * cell_width / 2}" y="{height - 45}">'
        "Problem size N</text>"
    )
    save_svg(out_dir / "offset-aware-fusion-speedup-heatmap.svg", parts)


def main():
    parser = argparse.ArgumentParser(
        description="Generate SVG plots for size-stable Polly fusion benchmarks."
    )
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    args = parser.parse_args()

    case_order, sizes, data = read_grid(args.csv)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    plot_lines(args.out_dir, case_order, sizes, data)
    plot_summary(args.out_dir, case_order, sizes, data)
    plot_heatmap(args.out_dir, case_order, sizes, data)

    print(f"Generated plots in {args.out_dir}")


if __name__ == "__main__":
    main()
