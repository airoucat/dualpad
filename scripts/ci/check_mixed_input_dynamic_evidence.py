#!/usr/bin/env python3
"""Fail closed when mixed-input capability state outruns recorded live evidence."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


REQUIRED_GATES = (
    "I-0",
    "I-P",
    "I-1",
    "I-2",
    "I-MENU",
    "I-CURSOR",
    "I-SPRINT",
    "I-KBM",
    "I-5",
)
TARGET_SHA256 = "DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD"
APPROVED_RESULTS = {"PASS-A", "PASS-B", "PASS-S-A", "PASS-S-B"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path(".dualpad-builder/mixed_input_evidence.json"),
    )
    parser.add_argument(
        "--document",
        type=Path,
        default=Path("docs/research/skyrim_mixed_input_dynamic_evidence_zh.md"),
    )
    args = parser.parse_args()

    errors: list[str] = []
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        document = args.document.read_text(encoding="utf-8")
    except (OSError, json.JSONDecodeError) as error:
        print(f"mixed-input dynamic evidence: FAIL: {error}")
        return 2

    evidence = manifest.get("dynamicEvidence", {})
    gates = evidence.get("gates", {})
    if evidence.get("targetSha256") != TARGET_SHA256:
        errors.append("target SHA-256 does not match the approved Skyrim SE 1.5.97 binary")
    if TARGET_SHA256 not in document:
        errors.append("reviewed evidence document does not bind the approved target SHA-256")

    for gate_name in REQUIRED_GATES:
        gate = gates.get(gate_name)
        if not isinstance(gate, dict):
            errors.append(f"{gate_name}: missing gate record")
            continue
        if gate_name not in document:
            errors.append(f"{gate_name}: missing from reviewed evidence document")
        if gate_name == "I-KBM":
            decisions = gate.get("decisions", {})
            for decision_name in ("rawStateReconcile", "syntheticSuppression"):
                decision = decisions.get(decision_name, {})
                status = decision.get("status")
                enabled = bool(decision.get("capabilityEnabled", False))
                complete = bool(decision.get("manualEvidenceComplete", False))
                if enabled and (status not in APPROVED_RESULTS or not complete):
                    errors.append(
                        f"I-KBM/{decision_name}: capability enabled without an approved, complete independent verdict"
                    )
            continue

        status = gate.get("status")
        enabled = bool(gate.get("capabilityEnabled", False))
        complete = bool(gate.get("manualEvidenceComplete", False))
        if enabled and (status not in APPROVED_RESULTS or not complete):
            errors.append(
                f"{gate_name}: capability enabled without an approved, complete dynamic verdict"
            )

    release_status = evidence.get("releaseStatus")
    if release_status not in ("NO-GO", "GO"):
        errors.append("releaseStatus must be NO-GO or GO")
    if release_status == "GO":
        if any(
            gate_name not in gates
            or (
                gate_name != "I-KBM"
                and gates[gate_name].get("status") not in APPROVED_RESULTS
            )
            for gate_name in REQUIRED_GATES
        ):
            errors.append("releaseStatus=GO while one or more required dynamic gates lack an approved result")

    if errors:
        print("mixed-input dynamic evidence: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        "mixed-input dynamic evidence: PASS "
        f"releaseStatus={release_status} gates={len(REQUIRED_GATES)} target=SkyrimSE-1.5.97"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
