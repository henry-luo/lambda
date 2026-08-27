#!/usr/bin/env python3
"""Generate the benchmark history SVG from its chart metadata manifest."""

from __future__ import annotations

import argparse
import json
import math
from datetime import date
from html import escape
from pathlib import Path
from typing import Any


FONT = "-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif"
MONTH_ABBR = ("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")


def xml_text(value: object) -> str:
    """Escape text content without changing the chart's Unicode labels."""
    return escape(str(value), quote=False)


def number(value: float) -> str:
    return f"{value:.2f}"


def x_transform(layout: dict[str, Any]) -> str:
    anchor = layout["x_transform_anchor"]
    scale = layout["x_transform_scale"]
    return f"translate({anchor} 0) scale({scale} 1) translate(-{anchor} 0)"


def session_x(layout: dict[str, Any], index: int, count: int) -> float:
    if count == 1:
        return float(layout["plot_x_min"])
    first = float(layout["plot_x_min"])
    last = float(layout["plot_x_max"])
    return first + index * (last - first) / (count - 1)


def session_y(layout: dict[str, Any], value: float) -> float:
    minimum = min(layout["y_ticks"])
    maximum = max(layout["y_ticks"])
    top = float(layout["plot_y_min"])
    bottom = float(layout["plot_y_max"])
    fraction = (math.log10(maximum) - math.log10(value)) / (math.log10(maximum) - math.log10(minimum))
    return top + fraction * (bottom - top)


def contiguous_runs(values: list[object]) -> list[list[tuple[int, float]]]:
    runs: list[list[tuple[int, float]]] = []
    current: list[tuple[int, float]] = []
    for index, value in enumerate(values):
        if isinstance(value, (int, float)) and not isinstance(value, bool) and value > 0:
            current.append((index, float(value)))
        elif current:
            runs.append(current)
            current = []
    if current:
        runs.append(current)
    return runs


def path_data(run: list[tuple[int, float]], layout: dict[str, Any], count: int) -> str:
    commands = []
    for point_index, (index, value) in enumerate(run):
        command = "M" if point_index == 0 else "L"
        commands.append(f"{command} {number(session_x(layout, index, count))},{number(session_y(layout, value))}")
    return " ".join(commands)


def month_groups(metadata: dict[str, Any]) -> list[dict[str, str]]:
    labels = metadata["session_labels"]
    dates = {label: date.fromisoformat(value) for label, value in metadata["session_dates"].items()}
    groups: list[dict[str, str]] = []
    start = 0
    while start < len(labels):
        key = (dates[labels[start]].year, dates[labels[start]].month)
        end = start
        while end + 1 < len(labels):
            next_date = dates[labels[end + 1]]
            if (next_date.year, next_date.month) != key:
                break
            end += 1
        groups.append(
            {
                "label": f"{MONTH_ABBR[key[1] - 1]} {str(key[0])[-2:]}",
                "start": labels[start],
                "end": labels[end],
            }
        )
        start = end + 1
    return groups


def style_attributes(style: dict[str, Any]) -> str:
    dasharray = style.get("dasharray")
    if dasharray:
        return f' stroke-dasharray="{xml_text(dasharray)}"'
    return ""


def build_svg(metadata: dict[str, Any]) -> str:
    layout = metadata["layout"]
    labels = metadata["session_labels"]
    series = metadata["series"]
    styles = metadata["series_style"]
    count = len(labels)
    transform = x_transform(layout)
    plot_x_min = float(layout["plot_x_min"])
    plot_x_max = float(layout["plot_x_max"])
    plot_y_min = float(layout["plot_y_min"])
    plot_y_max = float(layout["plot_y_max"])

    for name, values in series.items():
        if len(values) != count:
            raise ValueError(f"{name} has {len(values)} values for {count} sessions")

    rendered_metadata = dict(metadata)
    rendered_metadata["month_groups"] = month_groups(metadata)
    metadata_json = json.dumps(rendered_metadata, ensure_ascii=False, separators=(",", ":"))

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{layout["width"]}" height="{layout["height"]}" viewBox="0 0 {layout["width"]} {layout["height"]}" role="img" aria-labelledby="chart-title chart-desc">',
        f'  <title id="chart-title">{xml_text(metadata["title"])}: Geometric Mean versus Node.js</title>',
        f'  <desc id="chart-desc">{xml_text(metadata["description"])}</desc>',
        f"  <metadata id=\"benchmark-history-data\"><![CDATA[{metadata_json}]]></metadata>",
        "  <defs>",
        '    <filter id="shadow" x="-10%" y="-10%" width="120%" height="120%">',
        '      <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#0f172a" flood-opacity="0.08"/>',
        "    </filter>",
        "  </defs>",
        f'  <rect width="{layout["width"]}" height="{layout["height"]}" fill="#ffffff"/>',
        f'  <rect x="{layout["card_x"]}" y="{layout["card_y"]}" width="{layout["card_width"]}" height="{layout["card_height"]}" rx="12" fill="#ffffff" filter="url(#shadow)"/>',
        f'  <text x="84" y="51" fill="#0f172a" font-family="{FONT}" font-size="26" font-weight="700">{xml_text(metadata["title"])}</text>',
        f'  <text x="84" y="75" fill="#475569" font-family="{FONT}" font-size="14">Geometric mean of engine time / Node.js time · lower than 1.0× is faster</text>',
    ]

    for item in metadata["legend_items"]:
        key = item["key"]
        style = styles[key]
        x = float(item["x"])
        lines.append(
            f'  <line x1="{number(x)}" y1="132" x2="{number(x + 30)}" y2="132" stroke="{style["color"]}" stroke-width="3.5"{style_attributes(style)} stroke-linecap="round"/>'
        )
        lines.append(f'  <text x="{number(x + 40)}" y="137" fill="#334155" font-family="{FONT}" font-size="13">{xml_text(key)}</text>')

    lines.extend(
        [
            f'  <g transform="{transform}" aria-label="Packed session axis and data">',
        ]
    )

    for tick in layout["y_ticks"]:
        y = session_y(layout, float(tick))
        stroke = "#94a3b8" if tick == 1 else "#e2e8f0"
        width = "1.5" if tick == 1 else "1"
        dasharray = ' stroke-dasharray="5 4"' if tick == 1 else ""
        label = f"{tick:g}×"
        lines.append(f'  <line x1="{number(plot_x_min)}" y1="{number(y)}" x2="{number(plot_x_max)}" y2="{number(y)}" stroke="{stroke}" stroke-width="{width}"{dasharray}/>')
        lines.append(f'  <text x="91" y="{number(y + 4)}" text-anchor="end" fill="#64748b" font-family="{FONT}" font-size="12">{label}</text>')

    for index in range(count):
        x = session_x(layout, index, count)
        major_grid = index == 0 or index == count - 1 or (index >= 4 and (index - 4) % 5 == 0)
        if major_grid:
            lines.append(f'  <line x1="{number(x)}" y1="{number(plot_y_min)}" x2="{number(x)}" y2="{number(plot_y_max)}" stroke="#f1f5f9" stroke-width="1"/>')
        lines.append(f'  <line x1="{number(x)}" y1="{number(plot_y_max)}" x2="{number(x)}" y2="{number(plot_y_max + 6)}" stroke="#94a3b8" stroke-width="1"/>')

    lines.extend(
        [
            f'  <line x1="{number(plot_x_min)}" y1="{number(plot_y_max)}" x2="{number(plot_x_max)}" y2="{number(plot_y_max)}" stroke="#64748b" stroke-width="1.2"/>',
            f'  <line x1="{number(plot_x_min)}" y1="{number(plot_y_min)}" x2="{number(plot_x_min)}" y2="{number(plot_y_max)}" stroke="#64748b" stroke-width="1.2"/>',
        ]
    )

    index_by_label = {label: index for index, label in enumerate(labels)}
    for divider in metadata["dividers"]:
        left = session_x(layout, index_by_label[divider["after"]], count)
        right = session_x(layout, index_by_label[divider["before"]], count)
        x = (left + right) / 2
        lines.append(f'  <line x1="{number(x)}" y1="{number(plot_y_min)}" x2="{number(x)}" y2="{number(plot_y_max)}" stroke="#cbd5e1" stroke-width="1" stroke-dasharray="3 5"/>')

    for index, label in enumerate(labels):
        x = session_x(layout, index, count)
        lines.append(f'  <text transform="translate({number(x)} {layout["x_tick_label_y"]}) rotate({layout["x_tick_label_angle"]})" text-anchor="start" fill="#475569" font-family="{FONT}" font-size="10">{xml_text(label)}</text>')

    lines.append("  </g>")
    lines.append(f'  <text x="{layout["x_axis_label_x"]}" y="{layout["x_axis_label_y"]}" text-anchor="middle" fill="#334155" font-family="{FONT}" font-size="14" font-weight="600">Benchmark session</text>')
    lines.append(f'  <text transform="translate({layout["y_axis_label_x"]} {layout["y_axis_label_y"]}) rotate(-90)" text-anchor="middle" fill="#334155" font-family="{FONT}" font-size="14" font-weight="600">Geometric mean over Node.js (×)</text>')
    lines.append(f'  <g transform="{transform}" aria-label="Packed benchmark data">')
    lines.append('  <g aria-label="Session creation month">')
    first_x = session_x(layout, 0, count)
    lines.append(f'    <line x1="{number(first_x)}" y1="{number(plot_y_min)}" x2="{number(first_x)}" y2="{number(plot_y_max)}" stroke="#94a3b8" stroke-width="1" stroke-dasharray="3 5"><title>{xml_text(labels[0])} · {xml_text(rendered_metadata["month_groups"][0]["label"])}</title></line>')
    lines.append(f'    <text x="812.50" y="{layout["month_label_y"]}" text-anchor="middle" fill="#64748b" font-family="{FONT}" font-size="11" font-weight="600">Session month</text>')
    for group in rendered_metadata["month_groups"]:
        start = index_by_label[group["start"]]
        end = index_by_label[group["end"]]
        x = (session_x(layout, start, count) + session_x(layout, end, count)) / 2
        lines.append(f'    <text x="{number(x)}" y="{layout["month_label_y"]}" text-anchor="middle" fill="#475569" font-family="{FONT}" font-size="11" font-weight="600">{xml_text(group["label"])}</text>')
    lines.append("  </g>")

    for key, values in series.items():
        style = styles[key]
        for run in contiguous_runs(values):
            if len(run) < 2:
                continue
            path = path_data(run, layout, count)
            lines.append(f'  <path d="{path}" fill="none" stroke="{style["color"]}" stroke-width="{style["stroke_width"]}"{style_attributes(style)} stroke-linejoin="round" stroke-linecap="round"><title>{xml_text(key)}</title></path>')

    c2mir_values = series["C2MIR"]
    for bridge in metadata["c2mir_gap_bridges"]:
        start_index = index_by_label[bridge["from"]]
        end_index = index_by_label[bridge["to"]]
        start_value = c2mir_values[start_index]
        end_value = c2mir_values[end_index]
        if start_value is None or end_value is None:
            raise ValueError(f"C2MIR bridge endpoints must have data: {bridge}")
        path = (
            f"M {number(session_x(layout, start_index, count))},{number(session_y(layout, start_value))} "
            f"L {number(session_x(layout, end_index, count))},{number(session_y(layout, end_value))}"
        )
        lines.append(f'  <path d="{path}" fill="none" stroke="{styles["C2MIR"]["color"]}" stroke-width="{styles["C2MIR"]["stroke_width"]}" stroke-linejoin="round" stroke-linecap="round"><title>C2MIR · {xml_text(bridge["note"])}</title></path>')

    lines.append(f'  <g aria-label="Inline series labels" font-family="{FONT}" font-size="11" font-weight="600" paint-order="stroke" stroke="#ffffff" stroke-width="4" stroke-linejoin="round">')
    for key, position in metadata["inline_labels"].items():
        lines.append(f'    <text x="{position["x"]}" y="{position["y"]}" fill="{styles[key]["color"]}">{xml_text(key)}</text>')
    lines.append("  </g>")

    for key, values in series.items():
        style = styles[key]
        if not style.get("show_points", True):
            continue
        for index, value in enumerate(values):
            if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
                continue
            x = session_x(layout, index, count)
            y = session_y(layout, float(value))
            lines.append(f'  <circle cx="{number(x)}" cy="{number(y)}" r="3.5" fill="#ffffff" stroke="{style["color"]}" stroke-width="2"><title>{xml_text(key)} · {xml_text(labels[index])}: {float(value):.2f}×</title></circle>')

    lines.append("  </g>")
    note_y = [825, 844, 863, 882]
    for y, note in zip(note_y, metadata["notes"]):
        lines.append(f'  <text x="84" y="{y}" fill="#64748b" font-family="{FONT}" font-size="11">{xml_text(note)}</text>')
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def main() -> None:
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, default=root / "benchmark_history_metadata.json", help="chart metadata manifest")
    parser.add_argument("--output", type=Path, default=root / "benchmark_history.svg", help="generated SVG path")
    args = parser.parse_args()

    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    svg = build_svg(metadata)
    args.output.write_text(svg, encoding="utf-8")


if __name__ == "__main__":
    main()
