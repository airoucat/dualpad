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

Invoke-Step xmake @("build", "DualPad")
Invoke-Step xmake @("build", "DualPadReplayTests")
Invoke-Step xmake @("build", "DualPadInputV2Tests")
Invoke-Step xmake @("build", "DualPadIngressTests")
Invoke-Step xmake @("build", "DualPadPromptSnapshotTests")
Invoke-Step xmake @("build", "DualPadPropertyTests")
Invoke-Step xmake @("build", "DualPadFuzzRegressionTests")
Invoke-Step xmake @("build", "DualPadDocGen")

Invoke-Step xmake @("run", "DualPadReplayTests")
Invoke-Step xmake @("run", "DualPadInputV2Tests")
Invoke-Step xmake @("run", "DualPadIngressTests")
Invoke-Step xmake @("run", "DualPadPromptSnapshotTests")
Invoke-Step xmake @("run", "DualPadPropertyTests")
Invoke-Step xmake @("run", "DualPadFuzzRegressionTests")

Invoke-Step python @("scripts/dev/generate_dualpad_docs.py")
Invoke-Step python @("scripts/ci/check_reviewed_docs_consistency.py")
Invoke-Step git @("diff", "--exit-code", "--", "docs/generated")
