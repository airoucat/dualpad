"""Fail CI when reviewed entry docs drift back to retired PH8 authority."""

from __future__ import annotations

import pathlib
import re
import sys
import json


ROOT = pathlib.Path(__file__).resolve().parents[2]
CONDITIONAL_RELEASE_STATUS = "GO WITH NATIVE FAVORITES DISABLED"


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

    for relative in [
        pathlib.Path("docs/harness/dualpad-builder.md"),
        pathlib.Path(".dualpad-builder/spec.md"),
    ]:
        text = (ROOT / relative).read_text(encoding="utf-8")
        if re.search(r"HidReader\s*->\s*PadState", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: builder memory must not publish HID/PadState normalization as the current runtime authority chain.")
        if re.search(r"当前[^。\n]*(glyph|图标)[^。\n]*(surface|面)[^。\n]*startmenu\.swf|startmenu\.swf[^。\n]*ScaleformGlyphBridge", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: builder memory must not describe startmenu.swf as current glyph authority.")

    harness = (ROOT / "docs/harness/dualpad-builder.md").read_text(encoding="utf-8")
    if re.search(r"当前仓库的验证入口[\s\S]*(DualPadMenuContextPolicyTests|DualPadRouteHealthContractTests|DualPadGlyphResolutionCompatTests)", harness, flags=re.IGNORECASE):
        failures.append("docs/harness/dualpad-builder.md: old focused targets must not be listed as current default proof.")

    phase8_ci = (ROOT / "scripts/ci/run_phase8_ci.ps1").read_text(encoding="utf-8")
    for target in [
        "DualPadReplayTests",
        "DualPadInputV2Tests",
        "DualPadManifestCompilerTests",
        "DualPadIngressTests",
        "DualPadPromptSnapshotTests",
        "DualPadPropertyTests",
        "DualPadFuzzRegressionTests",
        "DualPadPresentationProjectionTests",
    ]:
        if target not in phase8_ci:
            failures.append(f"scripts/ci/run_phase8_ci.ps1: default CI must build/run {target}.")
    if "scripts/ci/check_legacy_authority_boundary.py" not in phase8_ci:
        failures.append("scripts/ci/run_phase8_ci.ps1: default CI must run legacy authority boundary check.")
    if "scripts/ci/check_release_readiness.py" not in phase8_ci:
        failures.append("scripts/ci/run_phase8_ci.ps1: default CI must run release readiness check.")
    if "scripts/ci/check_config_prompt_menu_glyph_closure.py" not in phase8_ci:
        failures.append("scripts/ci/run_phase8_ci.ps1: default CI must run config/prompt/menu/glyph closure check.")

    workflow = (ROOT / ".github/workflows/dualpad-ci.yml").read_text(encoding="utf-8")
    for marker in ["rc-readiness:", "needs: phase8", "scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest"]:
        if marker not in workflow:
            failures.append(f".github/workflows/dualpad-ci.yml: missing RC readiness workflow marker {marker}.")

    rc_gate = (ROOT / "scripts/ci/run_rc_readiness.ps1").read_text(encoding="utf-8")
    for marker in [
        "scripts/ci/run_phase8_ci.ps1",
        "DualPadReplayHarness",
        "scripts/dev/dualpad_trace_diff.py",
        "scripts/ci/check_legacy_authority_boundary.py",
        "scripts/ci/check_release_readiness.py",
        "scripts/ci/check_config_prompt_menu_glyph_closure.py",
        "scripts/ci/check_rc_readiness_closeout.py",
        "scripts/dev/generate_release_artifact_manifest.py",
        "PYTHONUTF8",
        "PYTHONIOENCODING",
        "scripts/dev/setup_graphify_local.py",
    ]:
        if marker not in rc_gate:
            failures.append(f"scripts/ci/run_rc_readiness.ps1: RC outer gate missing marker {marker}.")
    graphify_setup = (ROOT / "scripts/dev/setup_graphify_local.py").read_text(encoding="utf-8")
    if "graphifyy==0.4.14" not in graphify_setup:
        failures.append("scripts/dev/setup_graphify_local.py: graphify install must stay pinned for clean CI runners.")
    u5_doc = (ROOT / "docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md").read_text(encoding="utf-8")
    for marker in ["Phase8 是 canonical base gate", "Real-game QA matrix", "Performance budget", "RuntimeDebugSnapshot"]:
        if marker not in u5_doc:
            failures.append(f"docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md: missing U5 marker {marker}.")

    feature_data = json.loads((ROOT / ".dualpad-builder/feature_list.json").read_text(encoding="utf-8"))
    features = {item["id"]: item for item in feature_data["features"]}
    for feature_id in ["DP1", "DP2", "DP3", "DP4", "PH0", "PH1", "PH2", "PH3", "PH4", "PH5", "PH6", "PH7", "PH8", "PH8a", "PH8b"]:
        feature = features.get(feature_id)
        if not feature or feature.get("status") != "completed" or feature.get("passes") is not True:
            failures.append(f".dualpad-builder/feature_list.json: {feature_id} must be completed with passes=true after PH8b closeout.")
    dp5 = features.get("DP5")
    if not dp5 or dp5.get("status") != "in_progress" or dp5.get("passes") is not False:
        failures.append(".dualpad-builder/feature_list.json: DP5 must remain in_progress/passes=false while native Favorites dynamic closeout is outstanding.")
    elif "post-closeout" not in (dp5.get("title", "") + " " + " ".join(dp5.get("acceptance", []))).lower():
        failures.append(".dualpad-builder/feature_list.json: DP5 must be explicitly labeled post-closeout hardening.")
    elif not all(marker in " ".join(dp5.get("acceptance", [])) for marker in ["S-DP5-RC20-HOTFIX", "fail-closed", CONDITIONAL_RELEASE_STATUS, "不得标记 GO"]):
        failures.append(".dualpad-builder/feature_list.json: DP5 must retain the completed hotfix, native Favorites fail-closed, conditional release status, and full-GO boundary.")

    sprint_data = json.loads((ROOT / ".dualpad-builder/sprint_plan.json").read_text(encoding="utf-8"))
    if sprint_data.get("current_sprint") is not None:
        failures.append(".dualpad-builder/sprint_plan.json: current_sprint must be null after the conditional RC20 hotfix closeout.")
    sprints = {item["id"]: item for item in sprint_data["sprints"]}
    for sprint_id in ["S-DP1", "S-DP2", "S-DP3", "S-DP4", "S-PH0", "S-PH1", "S-PH2", "S-PH3", "S-PH4", "S-PH5", "S-PH6", "S-PH7", "S-PH8", "S-PH8a", "S-PH8b"]:
        sprint = sprints.get(sprint_id)
        if not sprint or sprint.get("status") != "completed":
            failures.append(f".dualpad-builder/sprint_plan.json: {sprint_id} must be completed after PH8b closeout.")
    sdp5 = sprints.get("S-DP5")
    if not sdp5 or sdp5.get("status") != "planned":
        failures.append(".dualpad-builder/sprint_plan.json: S-DP5 must remain planned as post-closeout hardening.")
    elif "post-closeout" not in (sdp5.get("title", "") + " " + sdp5.get("goal", "") + " " + " ".join(sdp5.get("exit_criteria", []))).lower():
        failures.append(".dualpad-builder/sprint_plan.json: S-DP5 must be explicitly labeled post-closeout hardening.")
    hotfix = sprints.get("S-DP5-RC20-HOTFIX")
    hotfix_contract = " ".join(
        [hotfix.get("title", ""), hotfix.get("goal", "")] + hotfix.get("exit_criteria", [])
    ) if hotfix else ""
    if not hotfix or hotfix.get("unit_id") != "DP5" or hotfix.get("status") != "completed":
        failures.append(".dualpad-builder/sprint_plan.json: S-DP5-RC20-HOTFIX must be the completed DP5 conditional-release sprint.")
    elif not all(marker in hotfix_contract for marker in ["不新增 runtime phase", "fail-closed", CONDITIONAL_RELEASE_STATUS]):
        failures.append(".dualpad-builder/sprint_plan.json: completed hotfix must retain its non-phase, fail-closed, and conditional release-status boundary.")

    work_packages = (ROOT / "docs/authoritative-baseline/work-packages/README.md").read_text(encoding="utf-8")
    for marker in ["`DP1`：`completed`", "`DP2`：`completed`", "`DP3`：`completed`", "`DP4`：`completed`", "`PH0` - `PH8b`：`completed`"]:
        if marker not in work_packages:
            failures.append(f"docs/authoritative-baseline/work-packages/README.md: missing current status marker {marker}.")
    if "`DP5`：`in_progress` / `passes=false`（post-closeout field-readiness hotfix；不是新的 runtime phase）" not in work_packages:
        failures.append("docs/authoritative-baseline/work-packages/README.md: DP5 must identify the active post-closeout hotfix without reopening a runtime phase.")
    if "当前无活跃 Sprint；最近完成：`S-DP5-RC20-HOTFIX`" not in work_packages:
        failures.append("docs/authoritative-baseline/work-packages/README.md: missing completed RC20 hotfix marker.")

    status_docs = [
        pathlib.Path("README.md"),
        pathlib.Path("docs/authoritative-baseline/README.md"),
        pathlib.Path("docs/authoritative-baseline/work-packages/README.md"),
        pathlib.Path("docs/harness/dualpad-builder.md"),
        pathlib.Path("docs/DOC_INDEX_zh.md"),
        pathlib.Path("docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md"),
        pathlib.Path("docs/testing/rc20_runtime_validation.md"),
    ]
    for relative in status_docs:
        text = (ROOT / relative).read_text(encoding="utf-8")
        if CONDITIONAL_RELEASE_STATUS not in text:
            failures.append(f"{relative}: missing conditional RC20 release status.")
        if re.search(r"当前(?:发布状态|\s*release status)[^。\n]*NO-GO", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: must not retain NO-GO as the current status after conditional closeout.")

    sprint_docs = [
        pathlib.Path(".dualpad-builder/spec.md"),
        pathlib.Path("docs/authoritative-baseline/README.md"),
        pathlib.Path("docs/authoritative-baseline/work-packages/README.md"),
        pathlib.Path("docs/harness/dualpad-builder.md"),
        pathlib.Path("docs/DOC_INDEX_zh.md"),
    ]
    for relative in sprint_docs:
        text = (ROOT / relative).read_text(encoding="utf-8")
        if re.search(r"当前活跃(?:\s*Sprint)?\s*(?:是|：|:)?\s*`S-DP5-RC20-HOTFIX`|current_sprint=S-DP5-RC20-HOTFIX", text, flags=re.IGNORECASE):
            failures.append(f"{relative}: must not describe the completed RC20 hotfix as active.")
    for marker in ["默认 CI 自动执行", "人工 close-out 必做", "DualPadPresentationProjectionTests"]:
        if marker not in work_packages:
            failures.append(f"docs/authoritative-baseline/work-packages/README.md: missing close-out validation boundary marker {marker}.")

    if failures:
        print("reviewed doc consistency check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("reviewed docs consistency check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
