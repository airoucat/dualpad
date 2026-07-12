#!/usr/bin/env python3
"""Evaluate causal mixed-input runtime contracts from a JSONL trace."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
from typing import Any


CHANNELS = ("Look", "Move", "Combat", "TransientDigital")


def violation(contract: str, line: int, message: str) -> dict[str, Any]:
    return {"contract": contract, "line": line, "message": message}


def evaluate_record(record: dict[str, Any], line: int) -> list[dict[str, Any]]:
    violations: list[dict[str, Any]] = []

    gamepad = record.get("gamepad", {})
    if gamepad and not gamepad.get("meaningfulActivity", False):
        if (
            gamepad.get("activityRevisionAfter", 0)
            != gamepad.get("activityRevisionBefore", 0)
            or gamepad.get("ownerAfter") != gamepad.get("ownerBefore")
        ):
            violations.append(
                violation("B.1-7", line, "neutral/unchanged HID report changed activity or owner")
            )

    writers = record.get("writers", {})
    for channel in CHANNELS:
        if writers.get("currentCycle", {}).get(channel, 0) > 1:
            violations.append(
                violation(
                    "B.3-current-cycle-writer",
                    line,
                    f"{channel} has more than one current-cycle writer",
                )
            )
        if writers.get("nextPoll", {}).get(channel, 0) > 1:
            violations.append(
                violation(
                    "B.3-next-poll-writer",
                    line,
                    f"{channel} has more than one next-Poll writer",
                )
            )

    sprint = record.get("sprint", {})
    if sprint:
        source_mask = int(sprint.get("sourceMask", 0))
        previous_mask = int(sprint.get("previousSourceMask", source_mask))
        aggregate_held = bool(sprint.get("aggregateHeld", source_mask != 0))
        if source_mask != 0 and not aggregate_held:
            violations.append(
                violation("B.3-sprint-held", line, "non-empty Sprint source mask lost aggregate hold")
            )
        if previous_mask != 0 and source_mask == 0:
            if int(sprint.get("releaseTokenDelta", 0)) != 1 or sprint.get("virtualHeld", False):
                violations.append(
                    violation(
                        "B.3-sprint-final-release",
                        line,
                        "final Sprint contributor release was not emitted exactly once",
                    )
                )

    apply = record.get("apply", {})
    if apply.get("attempted", False):
        if apply.get("planEpoch") != apply.get("currentEpoch"):
            violations.append(
                violation("B.3-stale-epoch", line, "stale input-state epoch was applied")
            )
        if apply.get("planGamepadSession") != apply.get("currentGamepadSession"):
            violations.append(
                violation("B.3-stale-session", line, "stale gamepad session was applied")
            )

    engine = record.get("engineQuery", {})
    if engine.get("overrideApplied", False):
        if (
            not engine.get("scopeActive", False)
            or engine.get("domain") in (None, "Unknown", "Remap")
            or not engine.get("callerVerified", False)
        ):
            violations.append(
                violation(
                    "C.2-engine-original",
                    line,
                    "engine query override escaped a verified caller/domain scope",
                )
            )

    cursor = record.get("cursor", {})
    if record.get("caseId") == "runtime-presentation-shadow":
        requested_owner = cursor.get("requestedOwner")
        committed_owner = cursor.get("committedOwner")
        if requested_owner != committed_owner:
            plan = cursor.get("plan", {})
            if not cursor.get("pendingAfter", False) or not plan.get("token"):
                violations.append(
                    violation(
                        "B.3-cursor-pending",
                        line,
                        "uncommitted cursor owner request has no pending exact plan",
                    )
                )
    if cursor.get("commitChanged", False):
        plan = cursor.get("plan", {})
        ack = cursor.get("ack", {})
        exact = all(
            ack.get(field) == plan.get(field)
            for field in ("token", "contextRevision", "presentationEpoch", "menuInstance")
        )
        if (
            not ack.get("success", False)
            or not exact
            or (plan.get("positionSyncRequired", False) and not ack.get("positionSyncApplied", False))
        ):
            violations.append(
                violation("B.3-cursor-ack", line, "cursor owner committed without exact successful ack")
            )

    mutation = record.get("currentCycleMutation", {})
    if mutation.get("required", False) or mutation.get("applied", False):
        receipt = mutation.get("pollReceipt", {})
        if receipt.get("count") != 1 or not receipt.get("materializationId"):
            violations.append(
                violation(
                    "B.3-poll-receipt",
                    line,
                    "current-cycle mutation did not consume one exact Poll materialization receipt",
                )
            )
        if receipt.get("ambiguous", False):
            violations.append(
                violation("B.3-poll-receipt-ambiguous", line, "Poll receipt identity is ambiguous")
            )
        if receipt.get("reused", False):
            violations.append(
                violation("B.3-poll-receipt-reused", line, "Poll receipt was consumed more than once")
            )

    adapter = record.get("adapter", {})
    if adapter and adapter.get("result") not in (None, "Success"):
        if adapter.get("sensitiveLedgerBefore") != adapter.get("sensitiveLedgerAfter"):
            violations.append(
                violation(
                    "B.3-adapter-rollback",
                    line,
                    "adapter failure advanced a current-cycle-sensitive ledger",
                )
            )

    synthetic = record.get("synthetic", {})
    if synthetic.get("provenance") == "S-C":
        if (
            synthetic.get("physicalActivityEmitted", False)
            or synthetic.get("physicalRouteSuppressed", False)
            or synthetic.get("conflictingHelperRouteEnabled", False)
        ):
            violations.append(
                violation(
                    "B.1-7-synthetic",
                    line,
                    "unproven synthetic classification changed or suppressed the physical route",
                )
            )

    return violations


def evaluate(path: Path) -> tuple[list[dict[str, Any]], int]:
    violations: list[dict[str, Any]] = []
    records = 0
    with path.open(encoding="utf-8") as stream:
        for line_number, raw in enumerate(stream, start=1):
            if not raw.strip():
                continue
            records += 1
            try:
                record = json.loads(raw)
            except json.JSONDecodeError as error:
                violations.append(violation("trace-schema", line_number, str(error)))
                continue
            if not isinstance(record, dict) or record.get("schemaVersion") != 1:
                violations.append(
                    violation("trace-schema", line_number, "record must be an object with schemaVersion=1")
                )
                continue
            violations.extend(evaluate_record(record, line_number))
    return violations, records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    try:
        violations, records = evaluate(args.trace)
    except OSError as error:
        print(json.dumps({"status": "FAIL", "trace": str(args.trace), "violations": [{"contract": "trace-io", "line": 0, "message": str(error)}]}, ensure_ascii=False))
        return 2
    summary = {
        "status": "PASS" if not violations else "FAIL",
        "trace": args.trace.name,
        "records": records,
        "violations": violations,
    }
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if not violations else 1


if __name__ == "__main__":
    sys.exit(main())
