#!/usr/bin/env python3
"""Evaluate a DualPad RC20 live log without mutating the game or deployment."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass
from enum import Enum


class LiveLogStatus(str, Enum):
    PASS = "PASS"
    INCOMPLETE = "INCOMPLETE"
    FAIL = "FAIL"


@dataclass(frozen=True)
class LiveLogEvaluation:
    status: LiveLogStatus
    reasons: tuple[str, ...]
    build_commit: str | None
    runtime_version: str | None
    latest_generation: int
    handoff_count: int
    native_favorites: bool | None


BUILD_RE = re.compile(r"\[DualPad\]\[Build\] commit=([0-9a-fA-F]+) runtime=([^\s]+)")
NATIVE_RE = re.compile(r"nativeFavorites=(true|false)", re.IGNORECASE)
TICK_RE = re.compile(r"event=tick generation=(\d+).*?threadHash=(\d+)")
HANDOFF_RE = re.compile(
    r"event=rebound handoff=(\d+).*?currentThreadHash=(\d+).*?generation=(\d+)"
)
DEGRADED_RE = re.compile(r"\[RuntimeOwner\].*?event=degraded failure=([^\s]+)")
REFRESH_RE = re.compile(r"\[MenuRefreshTrace\].*?refreshed=(\d+)")


def _short_commit(value: str) -> str:
    return value.strip().lower()[:12]


def evaluate_live_log(
    text: str,
    *,
    expected_commit: str,
    require_native_disabled: bool = True,
) -> LiveLogEvaluation:
    failures: list[str] = []
    incomplete: list[str] = []
    lines = text.splitlines()

    build_matches = [BUILD_RE.search(line) for line in lines]
    builds = [match for match in build_matches if match]
    build_commit = builds[-1].group(1) if builds else None
    runtime_version = builds[-1].group(2) if builds else None
    if build_commit is None:
        incomplete.append("build commit line is missing")
    elif _short_commit(build_commit) != _short_commit(expected_commit):
        failures.append(
            f"build commit mismatch: expected {_short_commit(expected_commit)}, got {_short_commit(build_commit)}"
        )
    if runtime_version is None:
        incomplete.append("runtime version is missing")
    elif runtime_version != "1-5-97-0":
        failures.append(f"unsupported runtime version: {runtime_version}")

    native_matches = [NATIVE_RE.search(line) for line in lines]
    native_values = [match.group(1).lower() == "true" for match in native_matches if match]
    native_favorites = native_values[-1] if native_values else None
    if native_favorites is None:
        incomplete.append("native Favorites gate state is missing")
    elif require_native_disabled and native_favorites:
        failures.append("native Favorites is enabled during safe smoke")

    if not any("Installed official Poll XInput call-site hook" in line for line in lines):
        incomplete.append("upstream Poll hook installation line is missing")
    if not any("[RuntimeOwner] event=bound" in line for line in lines):
        incomplete.append("runtime owner bind line is missing")

    ticks: list[tuple[int, int, int]] = []
    for index, line in enumerate(lines):
        match = TICK_RE.search(line)
        if match:
            ticks.append((index, int(match.group(1)), int(match.group(2))))
    latest_generation = max((generation for _, generation, _ in ticks), default=0)
    if latest_generation == 0:
        incomplete.append("runtime generation tick is missing")

    handoffs: list[tuple[int, int, int, int]] = []
    for index, line in enumerate(lines):
        match = HANDOFF_RE.search(line)
        if match:
            handoffs.append(
                (index, int(match.group(1)), int(match.group(2)), int(match.group(3)))
            )
    for line in lines:
        degraded = DEGRADED_RE.search(line)
        if degraded:
            failures.append(f"runtime owner degraded: {degraded.group(1)}")
        lowered = line.lower()
        if "queue_overflow" in lowered or "sequence_gap" in lowered:
            failures.append("overflow or sequence gap observed")
        refresh = REFRESH_RE.search(line)
        if refresh and int(refresh.group(1)) > 1:
            failures.append(f"menu refresh touched multiple targets: refreshed={refresh.group(1)}")

    reasons = tuple(dict.fromkeys([*failures, *incomplete]))
    if failures:
        status = LiveLogStatus.FAIL
    elif incomplete:
        status = LiveLogStatus.INCOMPLETE
    else:
        status = LiveLogStatus.PASS
    return LiveLogEvaluation(
        status=status,
        reasons=reasons,
        build_commit=build_commit,
        runtime_version=runtime_version,
        latest_generation=latest_generation,
        handoff_count=max((handoff for _, handoff, _, _ in handoffs), default=0),
        native_favorites=native_favorites,
    )


def _default_log_path() -> pathlib.Path:
    return (
        pathlib.Path.home()
        / "Documents"
        / "My Games"
        / "Skyrim Special Edition"
        / "SKSE"
        / "DualPad.log"
    )


def _current_commit() -> str:
    return subprocess.check_output(
        ["git", "rev-parse", "--short=12", "HEAD"], text=True
    ).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=pathlib.Path, default=_default_log_path())
    parser.add_argument("--expect-commit", default=None)
    parser.add_argument("--allow-native-favorites", action="store_true")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()

    if not args.log.is_file():
        print(f"live log not found: {args.log}", file=sys.stderr)
        return 2
    result = evaluate_live_log(
        args.log.read_text(encoding="utf-8", errors="replace"),
        expected_commit=args.expect_commit or _current_commit(),
        require_native_disabled=not args.allow_native_favorites,
    )
    payload = {
        "status": result.status.value,
        "reasons": list(result.reasons),
        "build_commit": result.build_commit,
        "runtime_version": result.runtime_version,
        "latest_generation": result.latest_generation,
        "handoff_count": result.handoff_count,
        "native_favorites": result.native_favorites,
    }
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print(
            f"RC20 live log: {result.status.value} "
            f"build={result.build_commit or '<missing>'} "
            f"generation={result.latest_generation} handoffs={result.handoff_count}"
        )
        for reason in result.reasons:
            print(f"- {reason}")
    return {
        LiveLogStatus.PASS: 0,
        LiveLogStatus.FAIL: 1,
        LiveLogStatus.INCOMPLETE: 2,
    }[result.status]


if __name__ == "__main__":
    raise SystemExit(main())
