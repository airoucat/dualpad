# Errors Log

Command failures, exceptions, and unexpected behaviors.

---

## ERR-20260524-001

- Logged: 2026-05-24 10:45 CST
- Priority: medium
- Status: resolved
- Area: xmake / input_v2 target wiring
- Summary: Adding PH4 graph publication to `ActionManifestPublisher` broke `DualPadReplayHarness` linking until PH4 sources were added to every target that consumes `ph1_manifest_compiler_files`.
- Detail: `DualPad` built because it compiles `src/**.cpp`, but focused/replay targets use manual source lists. When a PH1 source starts depending on a new PH4 source, update focused tests and `replay_runtime_files` together.
- Related files: `xmake.lua`, `src/input_v2/config/ActionManifestPublisher.cpp`, `src/input_v2/actions/CompiledActionGraph*.{h,cpp}`
- Resolution: Added `ph4_action_graph_files` to PH1-consuming focused targets and replay runtime lists.

## ERR-20260526-001

- Logged: 2026-05-26 23:49 CST
- Priority: high
- Status: resolved
- Area: PH8a runtime closeout / replay coverage
- Summary: PH8a was marked completed before `DualPadRuntime` continuously published the stable-frame presentation surface and before transition-frame recovery was proven by targeted tests.
- Detail: Runtime closeout requires proof of the public surface pipeline (`PresentationProjection -> SkyrimCompatibilitySurface -> PromptRuntimeOwner`), transition frames bypassing gameplay projection/output apply, and mandatory replay coverage staying at 10 phase0 scenarios. A `no diff` result over fewer generated scenarios is not a valid closeout signal.
- Related files: `src/input_v2/gameplay/DualPadRuntime*.{h,cpp}`, `src/input_v2/actions/InteractionEngine.cpp`, `tests/input_v2/InputV2Tests.cpp`, `tests/input_v2/ReplayTests.cpp`, `.dualpad-builder/progress.md`
- Resolution: Added targeted runtime surface/recovery tests, fixed multi-axis value binding resolution, restored processor replay coverage to 10 scenarios, and recorded the full verification matrix before re-marking PH8a completed.

## ERR-20260527-001

- Logged: 2026-05-27 00:12 CST
- Priority: medium
- Status: resolved
- Area: xmake / input_v2 target wiring
- Summary: Adding `LiveInputFactProducer` to the ph7 ingress source list broke `DualPadReplayHarness` linking until `SourceEvidenceCollector.cpp` was wired into the same reusable source group.
- Detail: Focused targets that explicitly listed `SourceEvidenceCollector.cpp` still passed, but replay and other ph7 consumers failed because `LiveInputFactProducer` depends on the presentation evidence collector. When an input_v2 source group gains a cross-phase dependency, update the reusable group instead of patching only the currently failing target.
- Related files: `xmake.lua`, `src/input_v2/ingress/LiveInputFactProducer.*`, `src/input_v2/presentation/SourceEvidenceCollector.cpp`
- Resolution: Moved `SourceEvidenceCollector.cpp` into `ph7_ingress_files` and removed duplicate per-target additions.

## ERR-20260604-001

- Logged: 2026-06-04 00:48 CST
- Priority: medium
- Status: resolved
- Area: GitHub issue migration / PowerShell / network
- Summary: DP5-RC20 migration script from the downloaded zip failed on a PowerShell parser error, then partially created issues before a GitHub API EOF.
- Detail: The temporary `create_dp5_rc20_issues.ps1` copy failed around its here-string migration comment block. After replacing the here-string with an array join, a later `gh issue create` run hit GitHub API EOF after creating the milestone and U0-U2. Directly rerunning the original script would have duplicated issues because issue creation was not idempotent.
- Related files: `build/tmp/dualpad_dp5_rc20_github_issues/.../create_dp5_rc20_issues.ps1`, `docs/authoritative-baseline/dp5_rc20_contract_zh.md`
- Resolution: Patched only the temporary script copy, queried remote state, then recovered manually with REST `gh api`: created U3-U5 and Meta, added parent comments, migrated #2-#6 to `status:superseded`, and closed them as `not_planned`.

## ERR-20260605-001

- Logged: 2026-06-05 23:58 CST
- Priority: medium
- Status: resolved
- Area: GitHub Actions / xmake CI
- Summary: Phase8 CI failed on GitHub because `xmake build DualPad` prompted for first-time `hidapi` package installation in the clean runner.
- Detail: Local runs already had packages installed, so the script passed locally without `-y`. On a fresh GitHub Windows runner, `xmake build DualPad` requested confirmation and then failed with `packages(hidapi): must be installed!`. For this xmake version, `-y` must be placed after the task (`xmake build -y DualPad`); `xmake -y build DualPad` is parsed incorrectly and reports `invalid argument: DualPad`.
- Related files: `scripts/ci/run_phase8_ci.ps1`, `.github/workflows/ci.yml`
- Resolution: Updated Phase8 CI xmake build/run invocations to pass `-y` after `build` / `run`, then reran `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` successfully.
