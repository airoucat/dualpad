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

Invoke-Step xmake @("build", "-y", "DualPad")
Invoke-Step xmake @("build", "-y", "DualPadReplayTests")
Invoke-Step xmake @("build", "-y", "DualPadInputV2Tests")
Invoke-Step xmake @("build", "-y", "DualPadPresentationProjectionTests")
Invoke-Step xmake @("build", "-y", "DualPadIngressTests")
Invoke-Step xmake @("build", "-y", "DualPadPromptSnapshotTests")
Invoke-Step xmake @("build", "-y", "DualPadPropertyTests")
Invoke-Step xmake @("build", "-y", "DualPadFuzzRegressionTests")
Invoke-Step xmake @("build", "-y", "DualPadDocGen")

Invoke-Step xmake @("run", "-y", "DualPadReplayTests")
Invoke-Step xmake @("run", "-y", "DualPadInputV2Tests")
Invoke-Step xmake @("run", "-y", "DualPadPresentationProjectionTests")
Invoke-Step xmake @("run", "-y", "DualPadIngressTests")
Invoke-Step xmake @("run", "-y", "DualPadPromptSnapshotTests")
Invoke-Step xmake @("run", "-y", "DualPadPropertyTests")
Invoke-Step xmake @("run", "-y", "DualPadFuzzRegressionTests")

Invoke-Step python @("scripts/dev/generate_dualpad_docs.py")
Invoke-Step python @("scripts/ci/check_reviewed_docs_consistency.py")
Invoke-Step python @("scripts/ci/check_legacy_authority_boundary.py")
Invoke-Step python @("scripts/ci/check_release_readiness.py")
Invoke-Step python @("scripts/ci/check_config_prompt_menu_glyph_closure.py")
Invoke-Step git @("diff", "--exit-code", "--", "docs/generated")
