"""Verify legacy-named surfaces cannot regain input_v2 runtime authority."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


TEXT_SUFFIXES = {".cpp", ".h", ".hpp", ".c", ".cc", ".cxx", ".py", ".ps1", ".lua"}

DELETED_LEGACY_AUTHORITY_PATHS = [
    pathlib.Path("src/input/InputModalityTracker.h"),
    pathlib.Path("src/input/InputModalityTracker.cpp"),
    pathlib.Path("src/input/injection/GameplayOwnershipCoordinator.h"),
    pathlib.Path("src/input/injection/GameplayOwnershipCoordinator.cpp"),
]

LEGACY_AUTHORITY_NAMES = [
    "InputModalityTracker",
    "GameplayOwnershipCoordinator",
]

LEGACY_SNAPSHOT_ALLOWED_SOURCES = {
    pathlib.Path("src/input_v2/ingress/FrameAssembler.h"),
    pathlib.Path("src/input_v2/ingress/FrameAssembler.cpp"),
    pathlib.Path("src/input_v2/ingress/IngressHub.cpp"),
    pathlib.Path("src/input_v2/ingress/IngressMarkers.h"),
    pathlib.Path("src/input_v2/ingress/LegacyIngressAdapter.cpp"),
    pathlib.Path("src/input/injection/PadEventSnapshotDispatcher.cpp"),
    pathlib.Path("src/input/injection/PadEventSnapshotProcessor.cpp"),
}

CORE_RUNTIME_DIRS = [
    pathlib.Path("src/input_v2/actions"),
    pathlib.Path("src/input_v2/gameplay"),
    pathlib.Path("src/input_v2/presentation"),
    pathlib.Path("src/input_v2/prompt"),
]


def source_files_under(relative: pathlib.Path) -> list[pathlib.Path]:
    base = ROOT / relative
    if not base.exists():
        return []
    return [
        path
        for path in base.rglob("*")
        if path.is_file() and path.suffix in TEXT_SUFFIXES
    ]


def read(relative: pathlib.Path) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def function_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start == -1:
        return ""
    brace = text.find("{", start)
    if brace == -1:
        return ""

    depth = 0
    for index in range(brace, len(text)):
        char = text[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:index]
    return ""


def relative(path: pathlib.Path) -> pathlib.Path:
    return path.relative_to(ROOT)


def main() -> int:
    failures: list[str] = []

    phase8_ci = read(pathlib.Path("scripts/ci/run_phase8_ci.ps1"))
    if "scripts/ci/check_legacy_authority_boundary.py" not in phase8_ci:
        failures.append("scripts/ci/run_phase8_ci.ps1 must invoke check_legacy_authority_boundary.py.")

    for legacy_path in DELETED_LEGACY_AUTHORITY_PATHS:
        if (ROOT / legacy_path).exists():
            failures.append(f"{legacy_path}: retired authority file must stay deleted.")

    for path in source_files_under(pathlib.Path("src")):
        path_relative = relative(path)
        text = path.read_text(encoding="utf-8", errors="ignore")
        for name in LEGACY_AUTHORITY_NAMES:
            if name in text:
                failures.append(f"{path_relative}: must not reference retired authority {name}.")

    for path in source_files_under(pathlib.Path("src/input_v2")):
        path_relative = relative(path)
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "legacySnapshot" in text and path_relative not in LEGACY_SNAPSHOT_ALLOWED_SOURCES:
            failures.append(
                f"{path_relative}: legacySnapshot is only allowed in compat/replay ingress adapters."
            )

    for directory in CORE_RUNTIME_DIRS:
        for path in source_files_under(directory):
            path_relative = relative(path)
            if path_relative in LEGACY_SNAPSHOT_ALLOWED_SOURCES:
                continue
            text = path.read_text(encoding="utf-8", errors="ignore")
            for token in [
                "legacySnapshot",
                "PadEventSnapshotProcessor",
                "GameplayKbmFactTracker",
                "ControlMap::GetMappedKey",
                "GetMappedKey(",
            ]:
                if token in text:
                    failures.append(
                        f"{path_relative}: core runtime decisions must not read legacy authority token {token}."
                    )

    processor = read(pathlib.Path("src/input/injection/PadEventSnapshotProcessor.cpp"))
    for token in [
        "ResolveGameplayProjection",
        "InteractionEngine",
        "CompiledActionGraphPublisher",
        "PromptRuntimeOwner",
        "RecoverMissingPressStateAfterResync",
        "GameplayKbmFactTracker",
        "ControlMap::GetMappedKey",
        "GetMappedKey(",
    ]:
        if token in processor:
            failures.append(f"PadEventSnapshotProcessor.cpp must remain a bridge and not use {token}.")
    if "ProcessAssembledFrame(frame)" not in processor:
        failures.append("PadEventSnapshotProcessor.cpp must forward assembled frames to DualPadRuntime.")

    frame_assembler = read(pathlib.Path("src/input_v2/ingress/FrameAssembler.cpp"))
    kernel_body = function_body(frame_assembler, "actions::KernelFrame BuildKernelFrame")
    if not kernel_body:
        failures.append("FrameAssembler.cpp must expose BuildKernelFrame for authority-boundary inspection.")
    else:
        for token in ["legacySnapshot", "sourceEvidence", "pulseLedger"]:
            if token in kernel_body:
                failures.append(
                    f"BuildKernelFrame must derive the kernel only from input_v2 facts/frame fields, not {token}."
                )

    runtime = read(pathlib.Path("src/input_v2/gameplay/DualPadRuntime.cpp"))
    for token in ["legacySnapshot", "PadEventSnapshotProcessor", "GameplayKbmFactTracker", "GetMappedKey("]:
        if token in runtime:
            failures.append(f"DualPadRuntime.cpp must not consume legacy authority token {token}.")

    input_v2_tracker_refs = [
        relative(path)
        for path in source_files_under(pathlib.Path("src/input_v2"))
        if "GameplayKbmFactTracker" in path.read_text(encoding="utf-8", errors="ignore")
    ]
    allowed_tracker_refs = {pathlib.Path("src/input_v2/telemetry/GameplayKbmFactTrackerReplayStub.cpp")}
    for path in input_v2_tracker_refs:
        if path not in allowed_tracker_refs:
            failures.append(f"{path}: GameplayKbmFactTracker may only appear in replay telemetry under input_v2.")

    if failures:
        print("legacy authority boundary check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("legacy authority boundary check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
