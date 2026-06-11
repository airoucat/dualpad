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
- Summary: Phase8 CI failed on GitHub because the clean runner lacked both noninteractive xmake package confirmation and the local CommonLib checkout.
- Detail: Local runs already had packages installed and `lib/commonlibsse-ng` present, so the script passed locally. On a fresh GitHub Windows runner, `xmake build DualPad` first requested confirmation and failed with `packages(hidapi): must be installed!`. After adding `-y`, the runner installed `hidapi` and then failed with `unknown rule(commonlibsse-ng.plugin)` because `lib/` is ignored, `.gitmodules` has a CommonLib entry, but current `HEAD` has no `lib/commonlibsse-ng` gitlink. For this xmake version, `-y` must be placed after the task (`xmake build -y DualPad`); `xmake -y build DualPad` is parsed incorrectly and reports `invalid argument: DualPad`.
- Related files: `scripts/ci/run_phase8_ci.ps1`, `.github/workflows/dualpad-ci.yml`
- Resolution: Updated Phase8 CI xmake build/run invocations to pass `-y` after `build` / `run`, added a workflow checkout for `alandtse/CommonLibVR` at `82e62861168308139339e5b8754586bbb556744e` with recursive submodules, then reran `powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1` successfully locally before rerunning GitHub Actions.

## ERR-20260612-001

- Logged: 2026-06-12 01:06 CST
- Priority: low
- Status: resolved
- Area: GitHub CLI / PowerShell shell syntax
- Summary: `gh pr create` failed because a Bash heredoc (`<<'EOF'`) was used while the active shell was PowerShell.
- Detail: In this workspace the shell is PowerShell. For multi-line CLI arguments, use a PowerShell here-string assigned to a variable and pass that variable, or write a temporary body file with PowerShell-native commands. Do not paste Bash heredoc syntax into `shell_command` on Windows.
- Related files: `.github/workflows/dualpad-ci.yml`, `.dualpad-builder/progress.md`
- Resolution: Retried PR creation with a PowerShell here-string body.

## ERR-20260612-002

- Logged: 2026-06-12 01:25 CST
- Priority: medium
- Status: resolved
- Area: GitHub Actions / Python encoding / graphify
- Summary: PR #22 `rc-readiness` failed on GitHub Windows runner because the fresh graphify setup path was not deterministic.
- Detail: The first failure occurred after the RC gate had passed Phase8, replay diff, static gates, DInput8 proxy build, and release manifest generation. The graphify step imported `graphify`, fell back to the install path, then printing the Chinese install message raised `UnicodeEncodeError: 'charmap' codec can't encode characters`. After forcing UTF-8, the next fresh runner installed `graphifyy 0.8.37`, but `import graphify` still failed. Pinning `graphifyy==0.4.14` installed the expected package, but the same Python process still could not import it until the user site path was explicitly added. Local runs did not reproduce because the local environment already had a working `graphify` import.
- Related files: `scripts/ci/run_rc_readiness.ps1`, `scripts/dev/setup_graphify_local.py`
- Resolution: Set `PYTHONUTF8=1` and `PYTHONIOENCODING=utf-8` in `scripts/ci/run_rc_readiness.ps1`, pinned `scripts/dev/setup_graphify_local.py` to install `graphifyy==0.4.14`, and added `site.getusersitepackages()` to `sys.path` after install.

## ERR-20260612-003

- Logged: 2026-06-12 02:50 CST
- Priority: medium
- Status: resolved
- Area: GitHub Actions / xmake package repositories
- Summary: PR #22 `rc-readiness` failed on one fresh GitHub runner because `xmake-requires.lock` referenced gitee/gitcode xmake-repo mirrors.
- Detail: The pull_request merge-ref job failed during `xmake build -y DualPad` while updating package repositories. The log showed xmake cloning from `https://gitee.com/tboox/xmake-repo.git`, then Git Credential Manager could not prompt on the noninteractive runner and failed with `fatal: could not read Username for 'https://gitee.com'`. A branch-trigger job for the same head passed, confirming the first failure was mirror/source selection rather than runtime or graphify logic. After rewriting the mirror URLs, both remote `rc-readiness` jobs advanced further but failed at `generate_release_artifact_manifest.py --expect-clean` because the workflow used floating `xmake-version: latest`, which resolved to xmake 3.0.9 and rewrote `xmake-requires.lock` on the fresh runner.
- Related files: `.github/workflows/dualpad-ci.yml`, `xmake-requires.lock`, `scripts/ci/check_rc_readiness_closeout.py`
- Resolution: Rewrote all xmake-repo URLs in `xmake-requires.lock` to `https://github.com/xmake-io/xmake-repo.git`, pinned both GitHub Actions xmake setup steps to `xmake-version: 3.0.7`, and added RC closeout static checks that reject gitee/gitcode mirrors and floating xmake versions.

## ERR-20260612-004

- Logged: 2026-06-12 03:35 CST
- Priority: medium
- Status: resolved
- Area: GitHub Actions / Windows line endings / release manifest gate
- Summary: PR #22 `rc-readiness` still failed after pinning xmake because generated docs and the xmake lockfile had no repository EOL contract.
- Detail: GitHub Windows runners checked out/generated `docs/generated/*.md` with CRLF working-tree endings. Phase8's path-scoped generated-doc diff emitted only line-ending warnings, but the later release manifest clean check used a full tracked-content dirty check and failed. After adding file-level dirty diagnostics, the next CI run confirmed `docs/generated` was fixed and the only remaining dirty tracked file was `xmake-requires.lock`, which xmake refreshed during build on the fresh runner.
- Related files: `.gitattributes`, `scripts/dev/generate_release_artifact_manifest.py`, `scripts/ci/check_rc_readiness_closeout.py`, `.dualpad-builder/progress.md`
- Resolution: Added `.gitattributes` entries for `docs/generated/*.md text eol=lf` and `xmake-requires.lock text eol=lf`, added manifest dirty-file and dirty-diff reporting, required those contracts in the RC closeout static gate, and restored CI-only xmake lockfile churn before the release manifest clean check.
