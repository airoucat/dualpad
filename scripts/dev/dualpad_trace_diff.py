#!/usr/bin/env python3
"""Compare DualPad Phase 0 replay trace bundles."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


TRACE_FILES = [
    "dispatcher_schedule.csv",
    "ingress_snapshot_frames.csv",
    "ingress_snapshot_events.csv",
    "processed_snapshot_frames.csv",
    "processed_snapshot_events.csv",
    "expected_authoritative_poll.csv",
    "expected_keyboard_bridge.csv",
    "expected_presentation_surface.csv",
    "glyph_queries.csv",
    "expected_glyph_results.csv",
]

FLOAT_COLUMNS = {
    "previous_value",
    "value",
    "left_stick_x",
    "left_stick_y",
    "right_stick_x",
    "right_stick_y",
    "left_trigger",
    "right_trigger",
    "move_x",
    "move_y",
    "look_x",
    "look_y",
}

FLOAT_TOLERANCE = 1e-4


@dataclass
class DiffResult:
    ok: bool
    scenario: str
    message: str


def read_csv(path: Path) -> tuple[list[str], list[list[str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.reader(handle))
    if not rows:
        return [], []
    return rows[0], rows[1:]


def values_match(column: str, expected: str, actual: str) -> bool:
    if column not in FLOAT_COLUMNS:
        return expected == actual
    try:
        return abs(float(expected) - float(actual)) <= FLOAT_TOLERANCE
    except ValueError:
        return expected == actual


def compare_file(expected_path: Path, actual_path: Path, scenario: str) -> DiffResult:
    if not expected_path.is_file():
        return DiffResult(False, scenario, f"missing expected file: {expected_path}")
    if not actual_path.is_file():
        return DiffResult(False, scenario, f"missing actual file: {actual_path}")

    expected_header, expected_rows = read_csv(expected_path)
    actual_header, actual_rows = read_csv(actual_path)
    if expected_header != actual_header:
        return DiffResult(
            False,
            scenario,
            f"{expected_path.name}: header mismatch\nexpected={expected_header}\nactual={actual_header}",
        )
    if len(expected_rows) != len(actual_rows):
        return DiffResult(
            False,
            scenario,
            f"{expected_path.name}: row count mismatch expected={len(expected_rows)} actual={len(actual_rows)}",
        )

    for row_index, (expected_row, actual_row) in enumerate(zip(expected_rows, actual_rows), start=2):
        if len(expected_row) != len(expected_header) or len(actual_row) != len(actual_header):
            return DiffResult(False, scenario, f"{expected_path.name}:{row_index}: column count mismatch")
        for column_index, column in enumerate(expected_header):
            expected_value = expected_row[column_index]
            actual_value = actual_row[column_index]
            if values_match(column, expected_value, actual_value):
                continue
            return DiffResult(
                False,
                scenario,
                f"{expected_path.name}:{row_index}:{column}: expected={expected_value} actual={actual_value}",
            )

    return DiffResult(True, scenario, f"{expected_path.name}: ok")


def compare_scenario(expected: Path, actual: Path) -> DiffResult:
    scenario = expected.name
    if not expected.is_dir():
        return DiffResult(False, scenario, f"expected scenario is not a directory: {expected}")
    if not actual.is_dir():
        return DiffResult(False, scenario, f"actual scenario is not a directory: {actual}")

    for file_name in TRACE_FILES:
        result = compare_file(expected / file_name, actual / file_name, scenario)
        if not result.ok:
            return result
    return DiffResult(True, scenario, "no diff")


def write_report(path: Path, result: DiffResult) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    status = "PASS" if result.ok else "FAIL"
    path.write_text(
        f"# DualPad Replay Diff\n\n"
        f"- scenario: `{result.scenario}`\n"
        f"- status: `{status}`\n"
        f"- result: {result.message}\n",
        encoding="utf-8",
    )


def scenario_dirs(root: Path) -> Iterable[Path]:
    for child in sorted(root.iterdir()):
        if child.is_dir():
            yield child


def run_single(args: argparse.Namespace) -> int:
    result = compare_scenario(args.expected, args.actual)
    if args.report:
        write_report(args.report, result)
    if not result.ok:
        print(result.message)
        return 1
    print(f"{result.scenario}: no diff")
    return 0


def run_batch(args: argparse.Namespace) -> int:
    failures: list[DiffResult] = []
    for expected_scenario in scenario_dirs(args.batch):
        actual_scenario = args.actual_root / expected_scenario.name
        result = compare_scenario(expected_scenario, actual_scenario)
        write_report(args.report_root / expected_scenario.name / "report.md", result)
        if result.ok:
            print(f"{expected_scenario.name}: no diff")
        else:
            print(f"{expected_scenario.name}: {result.message}")
            failures.append(result)

    return 1 if failures else 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    single = parser.add_argument_group("single scenario")
    single.add_argument("--expected", type=Path)
    single.add_argument("--actual", type=Path)
    single.add_argument("--report", type=Path)

    batch = parser.add_argument_group("batch")
    batch.add_argument("--batch", type=Path)
    batch.add_argument("--actual-root", type=Path)
    batch.add_argument("--report-root", type=Path)

    args = parser.parse_args()
    if args.batch:
        if not args.actual_root or not args.report_root:
            parser.error("--batch requires --actual-root and --report-root")
        return args
    if not args.expected or not args.actual:
        parser.error("single mode requires --expected and --actual")
    return args


def main() -> int:
    args = parse_args()
    if args.batch:
        return run_batch(args)
    return run_single(args)


if __name__ == "__main__":
    raise SystemExit(main())
