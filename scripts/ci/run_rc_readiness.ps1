param(
    [switch] $ExpectCleanManifest
)

$ErrorActionPreference = "Stop"

Set-StrictMode -Version Latest

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Command,
        [Parameter(Mandatory = $true)]
        [string[]] $Arguments
    )

    Write-Host "==> $Command $($Arguments -join ' ')"
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "command failed: $Command $($Arguments -join ' ')"
    }
}

Invoke-Step powershell @("-ExecutionPolicy", "Bypass", "-File", "scripts/ci/run_phase8_ci.ps1")

Invoke-Step xmake @("build", "-y", "DualPadReplayHarness")
Invoke-Step xmake @(
    "run",
    "-y",
    "DualPadReplayHarness",
    "--",
    "--batch",
    "tests/replay/golden/phase0",
    "--mode",
    "dispatcher",
    "--output-root",
    "build/replay"
)
Invoke-Step python @(
    "scripts/dev/dualpad_trace_diff.py",
    "--batch",
    "tests/replay/golden/phase0",
    "--actual-root",
    "build/replay",
    "--report-root",
    "build/replay-diff"
)

Invoke-Step python @("-m", "json.tool", ".dualpad-builder/feature_list.json", "NUL")
Invoke-Step python @("-m", "json.tool", ".dualpad-builder/sprint_plan.json", "NUL")
Invoke-Step python @("scripts/ci/check_reviewed_docs_consistency.py")
Invoke-Step python @("scripts/ci/check_legacy_authority_boundary.py")
Invoke-Step python @("scripts/ci/check_release_readiness.py")
Invoke-Step python @("scripts/ci/check_config_prompt_menu_glyph_closure.py")
Invoke-Step python @("scripts/ci/check_rc_readiness_closeout.py")

Invoke-Step xmake @("build", "-y", "DualPadDInput8Proxy")

$manifestArgs = @("scripts/dev/generate_release_artifact_manifest.py", "--require-build-artifacts")
if ($ExpectCleanManifest) {
    $manifestArgs += "--expect-clean"
}
Invoke-Step python $manifestArgs

Invoke-Step python3 @("scripts/dev/setup_graphify_local.py", "rebuild", "--reason", "manual-closeout")
Invoke-Step git @("diff", "--check")
