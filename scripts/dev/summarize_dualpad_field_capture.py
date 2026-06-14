#!/usr/bin/env python3
"""Summarize a live DualPad field-debug capture.

This helper is intentionally read-only. It turns a large SKSE log plus one
trace session directory into a compact boundary report so repeated in-game
tests can be triaged without hand-scanning megabytes of output.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from collections import Counter
from pathlib import Path
from typing import Any


LOG_PATTERNS = {
    "skyrim_compat": "[DualPad][SkyrimCompat]",
    "upstream_gamepad": "[DualPad][UpstreamGamepad]",
    "route_health": "[DualPad][RouteHealth]",
    "input_state": "[DualPad][Input][State]",
    "runtime_plan": "[DualPad][RuntimePlan]",
    "runtime_plan_action": "[DualPad][RuntimePlanAction]",
    "native_button_commit": "[DualPad][NativeButtonCommit]",
    "runtime_debug": "[DualPad][RuntimeDebug]",
    "controlmap_overlay": "[DualPad][ControlMapOverlay]",
    "sequence_gap": "[DualPad][SequenceGap]",
}


MAX_CREDIBLE_CONTROLMAP_MAPPINGS = 100_000


TRACE_FILES = [
    "processed_snapshot_frames.csv",
    "ingress_snapshot_frames.csv",
    "expected_authoritative_poll.csv",
    "expected_presentation_surface.csv",
    "glyph_queries.csv",
    "expected_glyph_results.csv",
    "dispatcher_schedule.csv",
    "processed_snapshot_events.csv",
    "ingress_snapshot_events.csv",
    "expected_keyboard_bridge.csv",
    "runtime_debug_snapshot.csv",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, help="Path to SKSE/DualPad.log")
    parser.add_argument("--trace-dir", type=Path, help="Path to one DualPadTrace session directory")
    parser.add_argument("--trace-root", type=Path, help="Path to DualPadTrace root; requires --session")
    parser.add_argument("--session", help="Trace session name under --trace-root")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Return non-zero when the capture is missing required live evidence",
    )
    args = parser.parse_args()
    if args.trace_dir is None and args.trace_root and args.session:
        args.trace_dir = args.trace_root / args.session
    if args.trace_dir is None:
        parser.error("provide --trace-dir or --trace-root with --session")
    return args


def file_info(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"path": None, "exists": False}
    if not path.exists():
        return {"path": str(path), "exists": False}
    stat = path.stat()
    return {
        "path": str(path),
        "exists": True,
        "bytes": stat.st_size,
        "mtime": stat.st_mtime,
    }


def is_nonzero_number(value: str | None, epsilon: float = 0.0001) -> bool:
    if value is None:
        return False
    text = value.strip()
    if not text:
        return False
    try:
        if text.lower().startswith("0x"):
            return int(text, 16) != 0
        return abs(float(text)) > epsilon
    except ValueError:
        return False


def bool_value(value: str | None) -> bool:
    return (value or "").strip().lower() in {"1", "true", "yes", "on"}


def extract_field(line: str, key: str) -> str | None:
    match = re.search(rf"\b{re.escape(key)}=([^\s]+)", line)
    return match.group(1) if match else None


def summarize_csv(path: Path) -> dict[str, Any]:
    summary: dict[str, Any] = {"path": str(path), "exists": path.exists(), "rows": 0}
    if not path.exists():
        return summary

    contexts: Counter[str] = Counter()
    owners: Counter[str] = Counter()
    glyph_tokens: Counter[str] = Counter()
    nonzero_digital_rows = 0
    analog_active_rows = 0
    poll_active_rows = 0
    gamepad_owner_rows = 0
    gamepad_enabled_rows = 0
    glyph_ok_rows = 0

    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        summary["columns"] = reader.fieldnames or []
        for row in reader:
            summary["rows"] += 1
            context = row.get("context") or row.get("context_name")
            if context:
                contexts[context] += 1

            if any(is_nonzero_number(row.get(field)) for field in ["digital_mask", "down_mask", "pressed_mask", "pulse_mask"]):
                nonzero_digital_rows += 1
            if any(
                is_nonzero_number(row.get(field))
                for field in ["left_stick_x", "left_stick_y", "right_stick_x", "right_stick_y", "left_trigger", "right_trigger"]
            ):
                analog_active_rows += 1
            if any(
                is_nonzero_number(row.get(field))
                for field in [
                    "committed_down_mask",
                    "committed_pressed_mask",
                    "committed_released_mask",
                    "move_x",
                    "move_y",
                    "look_x",
                    "look_y",
                    "left_trigger",
                    "right_trigger",
                ]
            ):
                poll_active_rows += 1

            owner = row.get("presentation_owner")
            if owner:
                owners[owner] += 1
                if owner == "Gamepad":
                    gamepad_owner_rows += 1
            if bool_value(row.get("is_using_gamepad")) or bool_value(row.get("gamepad_device_enabled")):
                gamepad_enabled_rows += 1

            if bool_value(row.get("ok")):
                glyph_ok_rows += 1
            token = row.get("button_art_token")
            if token:
                glyph_tokens[token] += 1

    summary.update(
        {
            "contexts": dict(contexts),
            "owners": dict(owners),
            "glyph_tokens": dict(glyph_tokens),
            "nonzero_digital_rows": nonzero_digital_rows,
            "analog_active_rows": analog_active_rows,
            "poll_active_rows": poll_active_rows,
            "gamepad_owner_rows": gamepad_owner_rows,
            "gamepad_enabled_rows": gamepad_enabled_rows,
            "glyph_ok_rows": glyph_ok_rows,
        }
    )
    return summary


def summarize_log(path: Path | None) -> dict[str, Any]:
    summary: dict[str, Any] = file_info(path)
    summary.update(
        {
            "marker_counts": {name: 0 for name in LOG_PATTERNS},
            "skyrim_compat_success": False,
            "upstream_hook_installed": False,
            "route_states": {},
            "input_nonzero_mask_lines": 0,
            "input_analog_active_lines": 0,
            "upstream_poll_active_lines": 0,
            "runtime_plan_active_lines": 0,
            "runtime_plan_actions": {},
            "native_commit_stages": {},
            "native_commit_actions": {},
            "native_queue_false": 0,
            "native_translate_failed": 0,
            "hook_installed": False,
            "hid_raw_buttons_seen": False,
            "cross_raw_seen": False,
            "menu_cancel_plan_seen": False,
            "menu_confirm_release_translate_failed_count": 0,
            "sequence_gap_count": 0,
            "hard_reset_outputs_count": 0,
            "impossible_controlmap_mapping_count": 0,
            "controlmap_mapping_counts": [],
            "last_lines": {},
        }
    )
    if path is None or not path.exists():
        return summary

    route_states: Counter[str] = Counter()
    runtime_plan_actions: Counter[str] = Counter()
    native_commit_stages: Counter[str] = Counter()
    native_commit_actions: Counter[str] = Counter()
    route_state_re = re.compile(r"routeState=([A-Za-z0-9_:-]+)")
    mask_re = re.compile(r"mask=0x([0-9A-Fa-f]+)")
    analog_re = re.compile(r"(?:ls|rs)=\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)|(?:lt|rt)=(-?\d+(?:\.\d+)?)")
    runtime_plan_counts_re = re.compile(r"\b(sustained|transient|helper)=(\d+)")
    controlmap_mappings_re = re.compile(r"\bmappings=(\d+)")
    runtime_plan_analog_re = re.compile(
        r"analog=move\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\) "
        r"look\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\) "
        r"triggers\((-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?)\)"
    )

    with path.open("r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            for name, marker in LOG_PATTERNS.items():
                if marker in line:
                    summary["marker_counts"][name] += 1
                    summary["last_lines"][name] = line.rstrip("\n")
            if "[DualPad][SkyrimCompat]" in line and "success" in line:
                summary["skyrim_compat_success"] = True
            if "[DualPad][UpstreamGamepad]" in line and "Installed official Poll XInput call-site hook" in line:
                summary["upstream_hook_installed"] = True
                summary["hook_installed"] = True
            if "[DualPad][UpstreamGamepad]" in line and "Poll thunk" in line and "active=true" in line:
                summary["upstream_poll_active_lines"] += 1
            if "[DualPad][SequenceGap]" in line or "transition=sequence_gap" in line:
                summary["sequence_gap_count"] += 1
            if "recovery=HardResetOutputs" in line:
                summary["hard_reset_outputs_count"] += 1
            if "[DualPad][ControlMapOverlay]" in line:
                impossible_mapping_line = "impossible mapping count" in line
                mapping_match = controlmap_mappings_re.search(line)
                if mapping_match:
                    mapping_count = int(mapping_match.group(1))
                    summary["controlmap_mapping_counts"].append(mapping_count)
                    if mapping_count > MAX_CREDIBLE_CONTROLMAP_MAPPINGS:
                        impossible_mapping_line = True
                if impossible_mapping_line:
                    summary["impossible_controlmap_mapping_count"] += 1
            if "[DualPad][RouteHealth]" in line:
                match = route_state_re.search(line)
                if match:
                    route_states[match.group(1)] += 1
            if "[DualPad][RuntimePlan]" in line:
                counts = {
                    match.group(1): int(match.group(2))
                    for match in runtime_plan_counts_re.finditer(line)
                }
                analog_match = runtime_plan_analog_re.search(line)
                analog_values = analog_match.groups() if analog_match else ()
                if any(value > 0 for value in counts.values()) or any(is_nonzero_number(value) for value in analog_values):
                    summary["runtime_plan_active_lines"] += 1
            if "[DualPad][RuntimePlanAction]" in line:
                action = extract_field(line, "action")
                if action:
                    runtime_plan_actions[action] += 1
                    if action == "Menu.Cancel":
                        summary["menu_cancel_plan_seen"] = True
            if "[DualPad][NativeButtonCommit]" in line:
                action = extract_field(line, "action")
                phase = extract_field(line, "phase")
                if action:
                    native_commit_actions[action] += 1
                if "Suppressed gameplay digital" in line:
                    native_commit_stages["suppressed"] += 1
                elif " translate_failed " in line:
                    native_commit_stages["translate_failed"] += 1
                    summary["native_translate_failed"] += 1
                    if action == "Menu.Confirm" and phase == "Release":
                        summary["menu_confirm_release_translate_failed_count"] += 1
                elif " apply " in line:
                    native_commit_stages["apply"] += 1
                elif " queue " in line:
                    native_commit_stages["queue"] += 1
                    if "queued=false" in line:
                        summary["native_queue_false"] += 1
                elif " poll=" in line:
                    native_commit_stages["poll"] += 1
                elif " emit " in line:
                    native_commit_stages["emit"] += 1
                else:
                    native_commit_stages["other"] += 1
            if "[DualPad][Input][State]" in line:
                mask_match = mask_re.search(line)
                if mask_match:
                    mask = int(mask_match.group(1), 16)
                    if mask != 0:
                        summary["input_nonzero_mask_lines"] += 1
                        summary["hid_raw_buttons_seen"] = True
                    if (mask & 0x00000002) != 0:
                        summary["cross_raw_seen"] = True
                if any(is_nonzero_number(value) for groups in analog_re.findall(line) for value in groups if value):
                    summary["input_analog_active_lines"] += 1

    summary["route_states"] = dict(route_states)
    summary["runtime_plan_actions"] = dict(runtime_plan_actions.most_common(20))
    summary["native_commit_stages"] = dict(native_commit_stages)
    summary["native_commit_actions"] = dict(native_commit_actions.most_common(20))
    return summary


def assess(log_summary: dict[str, Any], trace_summary: dict[str, Any]) -> dict[str, Any]:
    warnings: list[str] = []
    breakpoint = "none"

    if not log_summary.get("exists"):
        warnings.append("SKSE log is missing; cannot prove plugin startup.")
        breakpoint = "log_missing"
    elif not log_summary.get("skyrim_compat_success"):
        warnings.append("No successful SkyrimCompatibilitySurface hook marker found.")
        breakpoint = "skyrim_compat_hook"
    elif not log_summary.get("upstream_hook_installed"):
        warnings.append("No upstream Poll XInput hook install marker found.")
        breakpoint = "upstream_hook_install"
    elif log_summary.get("marker_counts", {}).get("route_health", 0) == 0:
        warnings.append("No route health drain markers found.")
        breakpoint = "route_health"

    trace_info = trace_summary["info"]
    if not trace_info.get("exists"):
        warnings.append("Trace session directory is missing.")
        if breakpoint == "none":
            breakpoint = "trace_missing"
        return {"first_breakpoint": breakpoint, "warnings": warnings}

    processed = trace_summary["files"].get("processed_snapshot_frames.csv", {})
    ingress = trace_summary["files"].get("ingress_snapshot_frames.csv", {})
    poll = trace_summary["files"].get("expected_authoritative_poll.csv", {})
    presentation = trace_summary["files"].get("expected_presentation_surface.csv", {})
    glyph_queries = trace_summary["files"].get("glyph_queries.csv", {})
    glyph_results = trace_summary["files"].get("expected_glyph_results.csv", {})

    if ingress.get("rows", 0) == 0:
        warnings.append("No ingress snapshot frames were captured.")
        if breakpoint == "none":
            breakpoint = "ingress_frames"
    if processed.get("rows", 0) == 0:
        warnings.append("No processed snapshot frames were captured.")
        if breakpoint == "none":
            breakpoint = "processed_frames"

    input_activity = (
        log_summary.get("input_nonzero_mask_lines", 0)
        + log_summary.get("input_analog_active_lines", 0)
        + processed.get("nonzero_digital_rows", 0)
        + processed.get("analog_active_rows", 0)
    )
    if input_activity == 0:
        warnings.append("No non-zero HID/parser input activity found in log or processed frames.")
        if breakpoint == "none":
            breakpoint = "hid_input_activity"

    if processed.get("nonzero_digital_rows", 0) > 0 and poll.get("poll_active_rows", 0) == 0:
        warnings.append("Processed frames contain digital input, but authoritative poll stayed inactive.")
        if breakpoint == "none":
            breakpoint = "authoritative_poll"
    if log_summary.get("cross_raw_seen") and not log_summary.get("menu_cancel_plan_seen"):
        warnings.append("Cross raw input was seen, but no RuntimePlanAction action=Menu.Cancel was captured.")
        if breakpoint == "none":
            breakpoint = "menu_cancel_plan_missing"
    if presentation.get("rows", 0) > 0 and presentation.get("gamepad_owner_rows", 0) == 0:
        warnings.append("Presentation surface never reported Gamepad owner.")
        if breakpoint == "none":
            breakpoint = "presentation_owner"
    if log_summary.get("runtime_plan_actions") and log_summary.get("marker_counts", {}).get("native_button_commit", 0) == 0:
        warnings.append("RuntimePlanAction markers exist, but no NativeButtonCommit markers were captured.")
        if breakpoint == "none":
            breakpoint = "native_commit"
    if log_summary.get("native_translate_failed", 0) > 0:
        warnings.append("NativeButtonCommit translate_failed markers were captured.")
        if breakpoint == "none":
            breakpoint = "native_commit_translate"
    if log_summary.get("menu_confirm_release_translate_failed_count", 0) > 0:
        warnings.append("Menu.Confirm phase=Release translated as failed in NativeButtonCommit.")
        if breakpoint == "none":
            breakpoint = "native_commit_translate"
    if log_summary.get("native_queue_false", 0) > 0:
        warnings.append("NativeButtonCommit queue returned queued=false.")
        if breakpoint == "none":
            breakpoint = "native_commit_queue"
    if log_summary.get("hard_reset_outputs_count", 0) > 0:
        warnings.append("HardResetOutputs recovery markers were captured; check for a reset storm.")
        if breakpoint == "none":
            breakpoint = "hard_reset_outputs"
    if log_summary.get("impossible_controlmap_mapping_count", 0) > 0:
        warnings.append("ControlMapOverlay reported an impossible mapping count.")
        if breakpoint == "none":
            breakpoint = "controlmap_mapping_count"
    if glyph_queries.get("rows", 0) > 0 and glyph_results.get("glyph_ok_rows", 0) == 0:
        warnings.append("Glyph queries were captured, but no successful glyph result rows were captured.")
        if breakpoint == "none":
            breakpoint = "glyph_resolution"

    if log_summary.get("marker_counts", {}).get("runtime_plan", 0) == 0:
        warnings.append("No RuntimePlan log markers found; verify this sample uses a current debug build.")
    if log_summary.get("marker_counts", {}).get("native_button_commit", 0) == 0:
        warnings.append("No NativeButtonCommit log markers found.")

    return {"first_breakpoint": breakpoint, "warnings": warnings}


def build_summary(args: argparse.Namespace) -> dict[str, Any]:
    log_summary = summarize_log(args.log)
    trace_dir = args.trace_dir
    trace_files = {name: summarize_csv(trace_dir / name) for name in TRACE_FILES}
    trace_summary = {"info": file_info(trace_dir), "files": trace_files}
    result = {
        "log": log_summary,
        "trace": trace_summary,
        "assessment": assess(log_summary, trace_summary),
    }
    return result


def print_text(summary: dict[str, Any]) -> None:
    log = summary["log"]
    trace = summary["trace"]
    assessment = summary["assessment"]

    print("DualPad field capture summary")
    print(f"log: {log.get('path')} exists={log.get('exists')} bytes={log.get('bytes', 0)}")
    print(f"trace: {trace['info'].get('path')} exists={trace['info'].get('exists')}")
    print(f"first_breakpoint: {assessment['first_breakpoint']}")
    print("")
    print("log markers:")
    for name, count in log.get("marker_counts", {}).items():
        print(f"  {name}: {count}")
    print(f"  skyrim_compat_success: {log.get('skyrim_compat_success')}")
    print(f"  upstream_hook_installed: {log.get('upstream_hook_installed')}")
    print(f"  hook_installed: {log.get('hook_installed')}")
    print(f"  route_states: {log.get('route_states')}")
    print(f"  hid_raw_buttons_seen: {log.get('hid_raw_buttons_seen')}")
    print(f"  cross_raw_seen: {log.get('cross_raw_seen')}")
    print(f"  menu_cancel_plan_seen: {log.get('menu_cancel_plan_seen')}")
    print(f"  input_nonzero_mask_lines: {log.get('input_nonzero_mask_lines')}")
    print(f"  input_analog_active_lines: {log.get('input_analog_active_lines')}")
    print(f"  upstream_poll_active_lines: {log.get('upstream_poll_active_lines')}")
    print(f"  runtime_plan_active_lines: {log.get('runtime_plan_active_lines')}")
    print(f"  runtime_plan_actions: {log.get('runtime_plan_actions')}")
    print(f"  native_commit_stages: {log.get('native_commit_stages')}")
    print(f"  native_commit_actions: {log.get('native_commit_actions')}")
    print(f"  native_queue_false: {log.get('native_queue_false')}")
    print(f"  native_translate_failed: {log.get('native_translate_failed')}")
    print(f"  menu_confirm_release_translate_failed_count: {log.get('menu_confirm_release_translate_failed_count')}")
    print(f"  sequence_gap_count: {log.get('sequence_gap_count')}")
    print(f"  hard_reset_outputs_count: {log.get('hard_reset_outputs_count')}")
    print(f"  impossible_controlmap_mapping_count: {log.get('impossible_controlmap_mapping_count')}")
    print(f"  controlmap_mapping_counts: {log.get('controlmap_mapping_counts')}")
    print("")
    print("trace files:")
    for name, item in trace["files"].items():
        if not item.get("exists"):
            print(f"  {name}: missing")
            continue
        extras = []
        for field in [
            "nonzero_digital_rows",
            "analog_active_rows",
            "poll_active_rows",
            "gamepad_owner_rows",
            "glyph_ok_rows",
        ]:
            value = item.get(field, 0)
            if value:
                extras.append(f"{field}={value}")
        suffix = f" ({', '.join(extras)})" if extras else ""
        print(f"  {name}: rows={item.get('rows', 0)}{suffix}")
    if assessment["warnings"]:
        print("")
        print("warnings:")
        for warning in assessment["warnings"]:
            print(f"  - {warning}")


def main() -> int:
    args = parse_args()
    summary = build_summary(args)
    if args.json:
        print(json.dumps(summary, indent=2, sort_keys=True))
    else:
        print_text(summary)
    if args.strict and summary["assessment"]["first_breakpoint"] != "none":
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
