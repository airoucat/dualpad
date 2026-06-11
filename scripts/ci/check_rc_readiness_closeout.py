"""Fail CI when DP5-RC20 U5 RC readiness closeout drifts."""

from __future__ import annotations

import pathlib
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
U5_DOC = ROOT / "docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md"


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require_tokens(failures: list[str], relative: str, tokens: list[str]) -> None:
    text = read(relative)
    for token in tokens:
        if token not in text:
            failures.append(f"{relative}: missing required token {token!r}")


def main() -> int:
    failures: list[str] = []

    if not U5_DOC.is_file():
        failures.append("docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md: missing U5 closeout doc.")
    else:
        require_tokens(
            failures,
            "docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md",
            [
                "DP5-RC20 U5",
                "Phase8 是 canonical base gate",
                "scripts/ci/run_rc_readiness.ps1",
                "scripts/ci/run_phase8_ci.ps1",
                "DualPadReplayTests",
                "DualPadInputV2Tests",
                "DualPadIngressTests",
                "DualPadPromptSnapshotTests",
                "DualPadPropertyTests",
                "DualPadFuzzRegressionTests",
                "DualPadPresentationProjectionTests",
                "replay diff",
                "graphify",
                "builder JSON",
                "generated docs",
                "legacy boundary",
                "release readiness",
                "U4 contract gate",
                "release artifact manifest",
                "Real-game QA matrix",
                "Performance budget",
                "RuntimeDebugSnapshot",
                "GraphUnavailable",
                "ManifestEpochSkew",
                "ContextRevisionSkew",
                "QueueOverflow",
                "SequenceGap",
                "BoundaryMismatch",
                "PromptScopeFrozen",
                "HookInstallFailed",
                "overflow compaction",
                "manifestEpoch",
                "configGeneration",
                "contextRevision",
                "reason-transition",
                "log storm",
                "旧 #5",
                "non-authority cleanup",
            ],
        )

    require_tokens(
        failures,
        "scripts/ci/run_rc_readiness.ps1",
        [
            "scripts/ci/run_phase8_ci.ps1",
            "DualPadReplayHarness",
            "scripts/dev/dualpad_trace_diff.py",
            ".dualpad-builder/feature_list.json",
            ".dualpad-builder/sprint_plan.json",
            "scripts/ci/check_reviewed_docs_consistency.py",
            "scripts/ci/check_legacy_authority_boundary.py",
            "scripts/ci/check_release_readiness.py",
            "scripts/ci/check_config_prompt_menu_glyph_closure.py",
            "scripts/ci/check_rc_readiness_closeout.py",
            "DualPadDInput8Proxy",
            "scripts/dev/generate_release_artifact_manifest.py",
            "--require-build-artifacts",
            "--expect-clean",
            "PYTHONUTF8",
            "PYTHONIOENCODING",
            "scripts/dev/setup_graphify_local.py",
            "git",
            "diff",
            "--check",
        ],
    )

    phase8 = read("scripts/ci/run_phase8_ci.ps1")
    for target in [
        "DualPadReplayTests",
        "DualPadInputV2Tests",
        "DualPadManifestCompilerTests",
        "DualPadIngressTests",
        "DualPadPromptSnapshotTests",
        "DualPadPropertyTests",
        "DualPadFuzzRegressionTests",
    ]:
        if target not in phase8:
            failures.append(f"scripts/ci/run_phase8_ci.ps1: Phase8 must still reference canonical target {target}.")

    if "scripts/ci/run_rc_readiness.ps1" in phase8:
        failures.append("scripts/ci/run_phase8_ci.ps1: Phase8 must not call the RC outer gate.")

    workflow = read(".github/workflows/dualpad-ci.yml")
    if "rc-readiness:" not in workflow:
        failures.append(".github/workflows/dualpad-ci.yml: missing rc-readiness job.")
    if "needs: phase8" not in workflow:
        failures.append(".github/workflows/dualpad-ci.yml: rc-readiness must depend on phase8.")
    if "scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest" not in workflow:
        failures.append(".github/workflows/dualpad-ci.yml: rc-readiness must run outer gate with -ExpectCleanManifest.")
    if "scripts/ci/run_phase8_ci.ps1" not in workflow:
        failures.append(".github/workflows/dualpad-ci.yml: phase8 must still run scripts/ci/run_phase8_ci.ps1.")

    require_tokens(
        failures,
        "scripts/dev/generate_release_artifact_manifest.py",
        [
            "DP5-RC20-release-artifact-manifest.json",
            "DP5-RC20-release-artifact-manifest.md",
            "docs/releases/dp5_rc20_u4_config_prompt_menu_glyph_contract_zh.md",
            "docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md",
            "git\", \"diff\", \"--quiet\", \"--",
            "git\", \"diff\", \"--cached\", \"--quiet\", \"--",
        ],
    )

    require_tokens(
        failures,
        "scripts/ci/check_reviewed_docs_consistency.py",
        [
            "scripts/ci/run_rc_readiness.ps1",
            "docs/releases/dp5_rc20_u5_rc_readiness_closeout_zh.md",
        ],
    )
    require_tokens(
        failures,
        "docs/authoritative-baseline/dp5_rc20_contract_zh.md",
        [
            "U5 Verification / Observability / Governance Closeout",
            "Phase8 是 canonical base gate",
            "RC readiness outer gate",
            "Real-game QA matrix",
            "Performance budget",
        ],
    )
    require_tokens(
        failures,
        "docs/DOC_INDEX_zh.md",
        ["releases/dp5_rc20_u5_rc_readiness_closeout_zh.md"],
    )
    require_tokens(
        failures,
        "docs/authoritative-baseline/work-packages/README.md",
        ["U5：verification / observability / governance closeout 已完成"],
    )

    if failures:
        print("RC readiness closeout check failed:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("RC readiness closeout check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
