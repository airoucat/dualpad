"""Static U3 release-readiness guard for product integration boundaries."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8", errors="ignore")


def require_contains(failures: list[str], path: str, tokens: list[str]) -> None:
    text = read(path)
    for token in tokens:
        if token not in text:
            failures.append(f"{path}: missing required token {token!r}")


def main() -> int:
    failures: list[str] = []

    require_contains(
        failures,
        "src/input/injection/UpstreamGamepadHook.cpp",
        [
            "SKSE::RUNTIME_SSE_1_5_97",
            "Unsupported runtime",
            "REL::verify_code",
            "upstream poll hook remains disabled",
            "_attemptedInstall = true",
        ],
    )
    require_contains(
        failures,
        "src/input/ControlMapOverlay.cpp",
        [
            "SKSE::RUNTIME_SSE_1_5_97",
            "Skipping runtime overlay on unsupported runtime",
            "return false",
        ],
    )
    require_contains(
        failures,
        "src/input_v2/config/AtomicConfigReloader.cpp",
        [
            "compile failed; keeping existing active bundle",
            "startup compile failed and no last-known-good available",
            "unsupported LKG schemaVersion",
            "LKG compiled validation failed",
        ],
    )
    require_contains(
        failures,
        "tests/input_v2/AtomicConfigReloaderTests.cpp",
        [
            "startup must fail when config is invalid and no LKG exists",
            "startup must fail when config is invalid and disk LKG schema is stale",
            "failed reload must keep active epoch",
            "Config files missing -> built-in defaults are allowed",
        ],
    )
    require_contains(
        failures,
        "src/input/HidReader.cpp",
        [
            "SubmitReset()",
            "LiveInputFactProducer::GetSingleton().Reset()",
            "HID device disconnected, reconnecting",
            "SetDevice(nullptr)",
        ],
    )
    require_contains(
        failures,
        "xmake.lua",
        [
            "dualpad_deploy",
            "if os.isfile(debug_ini_src) and not os.isfile(debug_ini_dst) then",
            "DualPadControlMap.txt",
            "DualPadDInput8.ini",
        ],
    )
    require_contains(
        failures,
        "docs/releases/dp5_rc20_u3_release_notes_zh.md",
        [
            "Skyrim SE 1.5.97",
            "unsupported runtime",
            "clean install",
            "overwrite install",
            "rollback install",
            "haptics",
            "最终图标视觉资产",
            "non-goals",
        ],
    )
    require_contains(
        failures,
        "scripts/dev/generate_release_artifact_manifest.py",
        [
            "DP5-RC20 release artifact manifest",
            "DualPad.dll",
            "dinput8.dll",
            "DualPadBindings.ini",
            "Interface/startmenu.swf",
            "source",
        ],
    )

    if failures:
        print("release readiness check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("release readiness check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
