"""Fail CI when reviewed entry docs drift back to retired PH8 authority."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


ENTRY_DOCS = [
    pathlib.Path("AGENTS.md"),
    pathlib.Path("docs/harness/dualpad-builder.md"),
    pathlib.Path(".dualpad-builder/spec.md"),
]


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

    for relative in ENTRY_DOCS:
        text = (ROOT / relative).read_text(encoding="utf-8")
        if re.search(r"FrameActionPlan\s*->\s*ActionLifecycleCoordinator", text, flags=re.IGNORECASE | re.DOTALL):
            failures.append(f"{relative}: must not publish the retired pre-input_v2 mainline as current truth.")
        if re.search(r"AuthoritativePollState[^。\n]*(保持|保留|作为)[^。\n]*当前主线|当前主线[^。\n]*AuthoritativePollState", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: must not describe AuthoritativePollState as current mainline authority.")
        if re.search(r"PH1[`'\s-]*PH8B.*planned backlog|PH1.*尚未启动", text, flags=re.IGNORECASE | re.DOTALL):
            failures.append(f"{relative}: must not say PH1-PH8B are still planned/not started.")
        if re.search(r"ScaleformGlyphBridge\s*\+\s*BindingManager|继续沿\s*`?ScaleformGlyphBridge\s*\+\s*BindingManager", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: must not route current glyph work through BindingManager authority.")

    harness = (ROOT / "docs/harness/dualpad-builder.md").read_text(encoding="utf-8")
    if re.search(r"当前仓库的验证入口[\s\S]*(DualPadMenuContextPolicyTests|DualPadRouteHealthContractTests|DualPadGlyphResolutionCompatTests)", harness, flags=re.IGNORECASE):
        failures.append("docs/harness/dualpad-builder.md: old focused targets must not be listed as current default proof.")

    if failures:
        print("reviewed doc consistency check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("reviewed docs consistency check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
