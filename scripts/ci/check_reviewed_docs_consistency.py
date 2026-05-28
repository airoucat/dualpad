"""Fail CI when reviewed entry docs drift back to retired PH8 authority."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


CHECKS: list[tuple[pathlib.Path, str, str, int]] = [
    (
        pathlib.Path("AGENTS.md"),
        r"FrameActionPlan\s*->\s*ActionLifecycleCoordinator",
        "AGENTS.md must not publish the retired pre-input_v2 mainline as current truth.",
        re.IGNORECASE | re.DOTALL,
    ),
    (
        pathlib.Path("AGENTS.md"),
        r"PH1[`'\s-]*PH8B.*planned backlog|PH1.*尚未启动",
        "AGENTS.md must not say PH1-PH8B are still planned/not started.",
        re.IGNORECASE | re.DOTALL,
    ),
    (
        pathlib.Path("AGENTS.md"),
        r"ScaleformGlyphBridge\s*\+\s*BindingManager",
        "AGENTS.md must not route current glyph work through BindingManager authority.",
        re.IGNORECASE,
    ),
    (
        pathlib.Path("docs/main_menu_glyph_current_status_zh.md"),
        r"先向\s*`?BindingManager`?\s*查询|这条链当前仍依赖\s*BindingManager|ScaleformGlyphBridge\s*\+\s*token",
        "main_menu_glyph_current_status_zh.md must not describe BindingManager as current glyph authority.",
        re.IGNORECASE,
    ),
    (
        pathlib.Path("docs/main_menu_glyph_current_status_zh.md"),
        r"reverse lookup.*扩展新的合同|在更正式的 glyph / prompt projection 落地前",
        "main_menu_glyph_current_status_zh.md must not describe reverse lookup as current glyph authority.",
        re.IGNORECASE,
    ),
    (
        pathlib.Path("docs/DOC_INDEX_zh.md"),
        r"S-PH8b.*active",
        "DOC_INDEX must not route readers to S-PH8b as active after PH8b closeout.",
        re.IGNORECASE,
    ),
]


def main() -> int:
    failures: list[str] = []
    for relative, pattern, message, flags in CHECKS:
        path = ROOT / relative
        text = path.read_text(encoding="utf-8")
        if re.search(pattern, text, flags=flags):
            failures.append(f"{relative}: {message}")

    if failures:
        print("reviewed doc consistency check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("reviewed docs consistency check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
