#!/usr/bin/env python3
"""Run inside IDA 9.3 to export the fixed Skyrim 1.5.97 static inventory.

Usage from IDA batch mode:
    idat.exe -A -S"scripts/dev/export_mixed_input_ida_static.py OUTPUT.json" DATABASE.i64

Without OUTPUT.json the JSON document is printed to the IDA output stream.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import sys
from typing import Any


TARGET_SHA256 = "DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD"
QUERY_TARGET = 0x140C15240
NATIVE_GAMEPLAY_QUERY_TARGET = 0x140C15280
HANDLER_COL_ENTRY = 0x14175E848
HANDLER_VFTABLE = 0x14175E850
HANDLER_DECLARED_SLOT_COUNT = 9
SIGNATURE_REGIONS = (
    (0x140C15240, 64),
    (0x140C15280, 32),
    (0x140705AE0, 32),
    (0x140ECD970, 32),
    (0x140ED2F90, 32),
    (0x140C150B0, 32),
    (0x140C1AB40, 32),
)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest().upper()


def collect() -> dict[str, Any]:
    import ida_bytes
    import ida_funcs
    import ida_ida
    import ida_kernwin
    import ida_nalt
    import ida_name
    import ida_xref
    import idaapi
    import idc

    input_path = Path(ida_nalt.get_input_file_path())
    input_sha256 = file_sha256(input_path)
    if input_sha256 != TARGET_SHA256:
        raise RuntimeError(
            f"unexpected input SHA-256: {input_sha256}; expected {TARGET_SHA256}"
        )

    def bytes_at(ea: int, size: int) -> str:
        data = ida_bytes.get_bytes(ea, size)
        if data is None or len(data) != size:
            raise RuntimeError(f"unable to read {size} bytes at {ea:#x}")
        return data.hex(" ").upper()

    def collect_direct_xrefs(target: int) -> list[dict[str, Any]]:
        result: list[dict[str, Any]] = []
        callsite = ida_xref.get_first_cref_to(target)
        while callsite != idaapi.BADADDR:
            function = ida_funcs.get_func(callsite)
            if function is None:
                raise RuntimeError(f"direct xref at {callsite:#x} has no owning function")
            result.append(
                {
                    "callsiteVa": hex(callsite),
                    "callInstruction": idc.generate_disasm_line(callsite, 0),
                    "callBytes": bytes_at(callsite, 5),
                    "resolvedTargetVa": hex(idc.get_operand_value(callsite, 0)),
                    "callerFunctionVa": hex(function.start_ea),
                    "callerFunctionEndVa": hex(function.end_ea),
                    "callerFunctionName": ida_funcs.get_func_name(function.start_ea),
                    "classification": "Unknown",
                }
            )
            callsite = ida_xref.get_next_cref_to(target, callsite)
        return result

    direct_xrefs = collect_direct_xrefs(QUERY_TARGET)
    native_gameplay_xrefs = collect_direct_xrefs(NATIVE_GAMEPLAY_QUERY_TARGET)

    signature_sites: list[dict[str, Any]] = []
    for address, size in SIGNATURE_REGIONS:
        function = ida_funcs.get_func(address)
        if function is None:
            raise RuntimeError(f"signature address {address:#x} has no owning function")
        signature_sites.append(
            {
                "va": hex(address),
                "bytes": bytes_at(address, size),
                "functionVa": hex(function.start_ea),
                "functionEndVa": hex(function.end_ea),
                "functionName": ida_funcs.get_func_name(function.start_ea),
            }
        )

    handler_entries = []
    for index in range(HANDLER_DECLARED_SLOT_COUNT + 1):
        entry_va = HANDLER_VFTABLE + index * 8
        handler_entries.append(
            {
                "index": index,
                "entryVa": hex(entry_va),
                "targetVa": hex(ida_bytes.get_qword(entry_va)),
                "kind": "vfunc"
                if index < HANDLER_DECLARED_SLOT_COUNT
                else "adjacent-complete-object-locator",
            }
        )

    handler_col_target = ida_bytes.get_qword(HANDLER_COL_ENTRY)
    handler_slot7_target = ida_bytes.get_qword(HANDLER_VFTABLE + 7 * 8)

    return {
        "schemaVersion": 3,
        "evidenceKind": "SkyrimMixedInputIdaStatic",
        "staticEvidenceStatus": "expanded-inventory-classification-pending",
        "dynamicGatesRemain": "NO-GO",
        "captureTool": "IDA Pro / IDAPython",
        "idaVersion": ida_kernwin.get_kernel_version(),
        "inputFileName": input_path.name,
        "databaseFileName": Path(idc.get_idb_path()).name,
        "inputSha256": input_sha256,
        "imageBase": hex(idaapi.get_imagebase()),
        "minEa": hex(ida_ida.inf_get_min_ea()),
        "maxEa": hex(ida_ida.inf_get_max_ea()),
        "queryTargetVa": hex(QUERY_TARGET),
        "queryEntry": {
            "functionEndVa": "0x140c15265",
            "delegateDeviceOffset": 112,
            "virtualSlotOffset": 56,
            "virtualSlotIndex": 7,
            "staticMeaning": "non-null delegate device then call byte-returning vfunc",
            "runtimeOriginalTarget": "0x140c19e00-static-live-vptr-recheck-pending",
        },
        "nativeGameplayQuery": {
            "targetVa": hex(NATIVE_GAMEPLAY_QUERY_TARGET),
            "functionEndVa": "0x140c152a5",
            "byteIdentityWithPrimaryQuery": bytes_at(QUERY_TARGET, 32)
            == bytes_at(NATIVE_GAMEPLAY_QUERY_TARGET, 32),
            "staticMeaning": "non-null delegate device then call byte-returning vfunc",
            "directCodeXrefCount": len(native_gameplay_xrefs),
            "playerControlsVtableRelocationId": 262983,
            "playerControlsVtableVa": "0x14166e838",
            "playerControlsProcessEventVa": "0x140704de0",
            "playerControlsCallsiteVa": "0x140704e4c",
            "classificationStatus": "pending-per-caller-domain-and-causality",
            "productionPatchEnabled": False,
            "directCodeXrefs": native_gameplay_xrefs,
        },
        "handlerTypeIdentity": {
            "completeObjectLocatorRelocationId": 560029,
            "completeObjectLocatorEntryVa": hex(HANDLER_COL_ENTRY),
            "completeObjectLocatorTargetVa": hex(handler_col_target),
            "completeObjectLocatorTargetName": ida_name.get_name(handler_col_target),
            "vftableRelocationId": 285457,
            "vftableVa": hex(HANDLER_VFTABLE),
            "vftableName": ida_name.get_name(HANDLER_VFTABLE),
            "addressPointDeltaBytes": HANDLER_VFTABLE - HANDLER_COL_ENTRY,
            "declaredSlotCount": HANDLER_DECLARED_SLOT_COUNT,
            "isEnabledSlotIndex": 7,
            "isEnabledTargetVa": hex(handler_slot7_target),
            "isEnabledBytes": bytes_at(handler_slot7_target, 16),
            "isEnabledStaticMeaning": "return qword[this+8] != 0",
            "entries0Through9": handler_entries,
        },
        "directCodeXrefCount": len(direct_xrefs),
        "releaseRelevantUnknownCallers": len(direct_xrefs),
        "directCodeXrefs": direct_xrefs,
        "signatureSites": signature_sites,
    }


def ida_arguments() -> list[str]:
    try:
        import idc

        return list(idc.ARGV[1:])
    except (ImportError, AttributeError):
        return sys.argv[1:]


def main() -> int:
    evidence = collect()
    rendered = json.dumps(evidence, ensure_ascii=False, indent=2) + "\n"
    arguments = ida_arguments()
    if arguments:
        output = Path(arguments[0])
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(rendered, encoding="utf-8")
        print(f"wrote {output}")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
