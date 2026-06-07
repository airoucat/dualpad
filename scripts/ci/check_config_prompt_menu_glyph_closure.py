"""Fail CI when DP5-RC20 U4 config/prompt/menu/glyph contracts drift."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
U4_DOC = ROOT / "docs/releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require_tokens(failures: list[str], path: pathlib.Path, tokens: list[str]) -> None:
    text = path.read_text(encoding="utf-8")
    for token in tokens:
        if token not in text:
            failures.append(f"{path.relative_to(ROOT)}: missing required token {token!r}")


def main() -> int:
    failures: list[str] = []

    if not U4_DOC.is_file():
        failures.append("docs/releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md: missing U4 reviewed contract doc.")
    else:
        require_tokens(
            failures,
            U4_DOC,
            [
                "DP5-RC20 U4",
                "zero direct binding",
                "inherited",
                "pass-through",
                "unsupported",
                "ignored",
                "bug：当前无",
                "unknown_menu_policy=passthrough",
                "FavoritesMenu",
                "non-restore workspace",
                "PromptScopeState::Unavailable",
                "ScopeUnavailable",
                "HiddenOnly",
                "DeviceFamilyMismatch",
                "glyph id",
                "platform id",
                "button semantic name",
                "fallback text",
                "asset lookup path",
                "missing icon behavior",
                "debug reason",
                "完整 visual icon artwork production 是非目标",
            ],
        )

    context_catalog = read("docs/generated/context_catalog_zh.md")
    for marker in [
        "| `BarterMenu` | `Menu` | 0 |",
        "| `FavoritesMenu` | `Menu` | 14 |",
        "| `MessageBoxMenu` | `Menu` | 0 |",
    ]:
        if marker not in context_catalog:
            failures.append(f"docs/generated/context_catalog_zh.md: missing expected context marker {marker!r}")

    action_sets = read("docs/generated/action_sets_zh.md")
    prompt_matrix = read("docs/generated/prompt_matrix_zh.md")
    for marker in [
        "| `FavoritesMenu` | `Favorites.Accept` | `Button:Cross` |",
        "| `FavoritesMenu` | `Favorites.LeftStick` | `Axis:LeftStickX` |",
    ]:
        if marker not in action_sets:
            failures.append(f"docs/generated/action_sets_zh.md: missing FavoritesMenu action marker {marker!r}")
    if "| `FavoritesMenu` | `Favorites.Accept` | `Button:Cross` | `Ok` |" not in prompt_matrix:
        failures.append("docs/generated/prompt_matrix_zh.md: missing visible FavoritesMenu prompt marker.")
    if "Favorites.LeftStick" in prompt_matrix:
        failures.append("docs/generated/prompt_matrix_zh.md: axis prompt must remain hidden from visible prompt matrix.")

    policies = read("docs/generated/policies_zh.md")
    for marker in [
        "| `Policy` | `unknown_menu_policy` | `passthrough` |",
        "| `Ignore` | `HUD Menu` | `true` |",
        "| `Ignore` | `Cursor Menu` | `true` |",
    ]:
        if marker not in policies:
            failures.append(f"docs/generated/policies_zh.md: missing policy marker {marker!r}")

    prompt_record = read("src/input_v2/prompt/PromptSnapshotRecord.h")
    for field in [
        "glyphId",
        "platformId",
        "buttonSemanticName",
        "fallbackText",
        "assetLookupPath",
        "missingIconBehavior",
        "debugReason",
    ]:
        if not re.search(rf"\bstd::string\s+{field}\b", prompt_record):
            failures.append(f"src/input_v2/prompt/PromptSnapshotRecord.h: PromptCandidate missing {field}.")

    prompt_service_h = read("src/input_v2/prompt/PromptService.h")
    for field in [
        "glyphId",
        "platformId",
        "buttonSemanticName",
        "fallbackText",
        "assetLookupPath",
        "missingIconBehavior",
        "debugReason",
    ]:
        if not re.search(rf"\bstd::string\s+{field}\b", prompt_service_h):
            failures.append(f"src/input_v2/prompt/PromptService.h: PromptLegacyGlyphDescriptor missing {field}.")

    prompt_service_cpp = read("src/input_v2/prompt/PromptService.cpp")
    for marker in [
        "Interface/Exported/DualPad/Glyphs/",
        "fallback_text",
        "fail_closed_empty_token",
        "GlyphAssetLookupPath",
    ]:
        if marker not in prompt_service_cpp:
            failures.append(f"src/input_v2/prompt/PromptService.cpp: missing contract marker {marker!r}")

    prompt_tests = read("tests/input_v2/PromptSnapshotTests.cpp")
    for marker in [
        "primary prompt candidate must expose glyph id",
        "snapshot must carry asset lookup path contract",
        "legacy failure descriptor must freeze missing icon fail-closed behavior",
    ]:
        if marker not in prompt_tests:
            failures.append(f"tests/input_v2/PromptSnapshotTests.cpp: missing U4 assertion marker {marker!r}")

    phase8_ci = read("scripts/ci/run_phase8_ci.ps1")
    if "scripts/ci/check_config_prompt_menu_glyph_closure.py" not in phase8_ci:
        failures.append("scripts/ci/run_phase8_ci.ps1: default CI must run U4 config/prompt/menu/glyph closure check.")

    reviewed_lint = read("scripts/ci/check_reviewed_docs_consistency.py")
    if "scripts/ci/check_config_prompt_menu_glyph_closure.py" not in reviewed_lint:
        failures.append("scripts/ci/check_reviewed_docs_consistency.py: reviewed docs lint must require the U4 closure check.")

    if failures:
        print("config/prompt/menu/glyph closure check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("config/prompt/menu/glyph closure check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
