#!/usr/bin/env python3
"""Validate the committed Skyrim 1.5.97 IDA static evidence inventory."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


TARGET_SHA256 = "DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD"
IMAGE_BASE = "0x140000000"
QUERY_TARGET = "0x140c15240"
EXPECTED_DIRECT_XREFS = 26
EXPECTED_XREF_INVENTORY_SHA256 = (
    "7C3FA20D43F07650FCB00C569A1E0455E8339FAA8E7AACA0D0CA5810DEB4E7F0"
)
ALLOWED_CLASSIFICATIONS = {
    "Unknown",
    "DeviceOperational",
    "GameplayLookTransform",
    "GameplayMoveTransform",
    "GameplayOtherTransform",
    "MenuPointerTransform",
    "MenuNavigationTransform",
    "MenuSetPlatform",
    "Remap",
}
REQUIRED_SIGNATURES = {
    "0x140c15240": "48 83 EC 28 48 8B 49 70 48 85 C9 74 11 48 8B 01 FF 50 38",
    "0x140705ae0": "48 89 5C 24 08 57 48 83 EC 70",
    "0x140ecd970": "48 8B C4 57 48 83 EC 70",
    "0x140ed2f90": "48 8B C4 56 57 41 56 48 81 EC 90 00 00 00",
    "0x140c150b0": "48 89 5C 24 10 48 89 74 24 18",
    "0x140c1ab40": "48 89 5C 24 20 57 48 83 EC 50",
}
ALLOWED_CAUSALITIES = {"AfterOwnerPublication", "EventLocalSource", "OriginalOnly"}
WINDOWS_ABSOLUTE = re.compile(r"(?:^|[^A-Za-z0-9])[A-Za-z]:[\\/]")


def normalized_va(value: Any) -> str:
    return str(value).lower()


def walk_strings(value: Any):
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for nested in value.values():
            yield from walk_strings(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from walk_strings(nested)


def xref_inventory_sha256(xrefs: list[dict[str, Any]]) -> str:
    rows = [
        (
            normalized_va(xref.get("callsiteVa")),
            str(xref.get("callBytes", "")).upper(),
            normalized_va(xref.get("resolvedTargetVa")),
            normalized_va(xref.get("callerFunctionVa")),
            normalized_va(xref.get("callerFunctionEndVa")),
        )
        for xref in xrefs
    ]
    encoded = json.dumps(rows, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest().upper()


def validate(evidence: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if evidence.get("schemaVersion") != 1:
        errors.append("schemaVersion must be 1")
    if evidence.get("evidenceKind") != "SkyrimMixedInputIdaStatic":
        errors.append("evidenceKind must identify Skyrim mixed-input IDA static evidence")
    if evidence.get("inputSha256") != TARGET_SHA256:
        errors.append("input SHA-256 does not match the approved Skyrim SE 1.5.97 binary")
    if normalized_va(evidence.get("imageBase")) != IMAGE_BASE:
        errors.append("image base does not match Skyrim SE 1.5.97")
    if normalized_va(evidence.get("queryTargetVa")) != QUERY_TARGET:
        errors.append("query target is not 0x140C15240")
    if evidence.get("staticEvidenceStatus") != "inventory-complete-classification-pending":
        errors.append("static evidence status must remain classification-pending")
    if evidence.get("dynamicGatesRemain") != "NO-GO":
        errors.append("static evidence must not approve dynamic gates")
    xrefs = evidence.get("directCodeXrefs")
    if evidence.get("directCodeXrefCount") != EXPECTED_DIRECT_XREFS:
        errors.append("directCodeXrefCount must equal 26")
    if not isinstance(xrefs, list) or len(xrefs) != EXPECTED_DIRECT_XREFS:
        errors.append("directCodeXrefs must contain exactly 26 entries")
        xrefs = []

    callsites: set[str] = set()
    unknown_callers = 0
    for index, xref in enumerate(xrefs):
        if not isinstance(xref, dict):
            errors.append(f"xref[{index}] must be an object")
            continue
        callsite = normalized_va(xref.get("callsiteVa"))
        if callsite in callsites:
            errors.append(f"xref[{index}] duplicates callsite {callsite}")
        callsites.add(callsite)
        if normalized_va(xref.get("resolvedTargetVa")) != QUERY_TARGET:
            errors.append(f"xref[{index}] resolved target is not {QUERY_TARGET}")
        call_bytes = str(xref.get("callBytes", "")).split()
        if len(call_bytes) != 5 or not call_bytes or call_bytes[0].upper() not in {"E8", "E9"}:
            errors.append(f"xref[{index}] is not a five-byte direct call/jump")
        classification = xref.get("classification")
        if classification not in ALLOWED_CLASSIFICATIONS:
            errors.append(f"xref[{index}] has an unsupported classification")
        elif classification == "Unknown":
            unknown_callers += 1
        else:
            if xref.get("causality") not in ALLOWED_CAUSALITIES or not str(
                xref.get("classificationEvidence", "")
            ).strip():
                errors.append(
                    f"xref[{index}] classification evidence and causality are required"
                )

    if evidence.get("releaseRelevantUnknownCallers") != unknown_callers:
        errors.append(
            "releaseRelevantUnknownCallers must equal the number of Unknown xrefs"
        )

    if (
        xrefs
        and all(isinstance(xref, dict) for xref in xrefs)
        and xref_inventory_sha256(xrefs) != EXPECTED_XREF_INVENTORY_SHA256
    ):
        errors.append("direct xref inventory does not match the reviewed IDA export")

    query_entry = evidence.get("queryEntry")
    expected_entry = {
        "functionEndVa": "0x140c15265",
        "delegateDeviceOffset": 112,
        "virtualSlotOffset": 56,
        "virtualSlotIndex": 7,
        "staticMeaning": "non-null delegate device then call byte-returning vfunc",
        "runtimeOriginalTarget": "unresolved-until-I-0-dynamic",
    }
    if not isinstance(query_entry, dict) or any(
        query_entry.get(key) != value for key, value in expected_entry.items()
    ):
        errors.append("query entry delegate offset or virtual slot semantics drifted")

    sites = evidence.get("signatureSites")
    by_va = {
        normalized_va(site.get("va")): site
        for site in sites
        if isinstance(site, dict)
    } if isinstance(sites, list) else {}
    for va, expected_prefix in REQUIRED_SIGNATURES.items():
        site = by_va.get(va)
        if site is None:
            errors.append(f"missing required signature site {va}")
            continue
        if not str(site.get("bytes", "")).upper().startswith(expected_prefix):
            errors.append(f"signature bytes drifted at {va}")

    for value in walk_strings(evidence):
        if WINDOWS_ABSOLUTE.search(value):
            errors.append("committed evidence contains a machine-private absolute path")
            break

    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--evidence",
        type=Path,
        default=Path(".dualpad-builder/mixed_input_ida_static_evidence.json"),
    )
    args = parser.parse_args()

    try:
        evidence = json.loads(args.evidence.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"mixed-input IDA static evidence: FAIL: {error}")
        return 2
    if not isinstance(evidence, dict):
        print("mixed-input IDA static evidence: FAIL: root must be an object")
        return 2

    errors = validate(evidence)
    if errors:
        print("mixed-input IDA static evidence: FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        "mixed-input IDA static evidence: PASS "
        f"directCodeXrefs={EXPECTED_DIRECT_XREFS} "
        f"signatures={len(REQUIRED_SIGNATURES)} dynamicGatesRemain=NO-GO"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
