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

## ERR-20260612-005

- Logged: 2026-06-12 04:59 CST
- Priority: low
- Status: resolved
- Area: RC readiness / local xmake lock churn
- Summary: Local `run_rc_readiness.ps1 -ExpectCleanManifest` could still fail after successful builds because xmake refreshed only the xmake-repo commit in `xmake-requires.lock`.
- Detail: The prior RC hardening restored `xmake-requires.lock` only on GitHub Actions. A local xmake 3.0.7 run can also update the package repository commit without changing package versions, leaving the working tree dirty before `generate_release_artifact_manifest.py --expect-clean`.
- Related files: `scripts/ci/run_rc_readiness.ps1`, `xmake-requires.lock`
- Resolution: Changed RC readiness to detect and restore `xmake-requires.lock` churn before the clean manifest step in both local and CI runs.

## ERR-20260613-001

- Logged: 2026-06-13 00:08 CST
- Priority: low
- Status: resolved
- Area: build / xmake artifact discovery
- Summary: 复查 DInput8 proxy 部署时，先假设本地产物位于 `build\windows\x64\releasedbg\DualPadDInput8Proxy.dll`，该路径不存在。
- Detail: 当前 xmake target 的实际 proxy 产物由 `xmake show -t DualPadDInput8Proxy` 和 `rg --files build` 确认，部署目标是 `G:\g\SkyrimSE\dinput8.dll`，本地构建产物路径是 `build\bin\DualPadDInput8Proxy\dinput8.dll`。后续不要根据通用 xmake build 目录猜测 proxy 产物路径。
- Resolution: 已用 `xmake show -t DualPadDInput8Proxy` 与部署目标文件 hash 复核。

## ERR-20260613-002

- Logged: 2026-06-13 00:23 CST
- Priority: low
- Status: resolved
- Area: tests / CommonLibSSE
- Summary: 新增 standalone native commit 测试时直接构造 `RE::BSFixedString`，测试进程在非 Skyrim 环境中挂住。
- Detail: `PollCommitCoordinator` 的请求/slot 使用 `RE::BSFixedString`。直接在新的独立测试进程里实例化该类型会依赖 CommonLib/Skyrim 字符串池环境，导致 `DualPadNativeButtonCommitTests` 运行超时并残留进程。后续 standalone contract tests 应优先测试纯函数/纯合同；若必须覆盖 `BSFixedString` 路径，应复用已有 harness 或先建立明确的 test runtime 初始化，而不是临时构造。
- Resolution: 已终止残留测试进程，并把该目标收缩为 `IsNativeDigitalGateOpenForContext()` 纯合同测试。

## ERR-20260615-001

- Logged: 2026-06-15 22:20 CST
- Priority: high
- Status: resolved
- Area: runtime / native menu injection
- Summary: `route_menu_confirm_input_event_accept_probe` 直接向 `BSInputEventQueue` 追加 Accept ButtonEvent 后，实机按 Triangle 闪退。
- Detail: 最新 `DualPad.log` 在 `[DualPad][NativeButtonCommit] apply action=Menu.Confirm phase=Press ... outputCode=Menu.Confirm` 后立刻截断，未打印 `route_input_event_accept` 成功或失败日志；未发现额外 crash dump。该 probe 与仓库正式契约冲突：当前 native 主线是 `AuthoritativePollState -> XInputStateBridge -> Skyrim Poll`，不是 direct `ButtonEvent/InputEventQueue` 拼接。
- Related files: `src/input/backend/NativeButtonCommitBackend.cpp`, `docs/backend_routing_decisions.md`, `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md`
- Resolution: 已从代码、测试和 `DualPadDebug.ini` 移除 direct `BSInputEventQueue` probe；后续菜单确认问题必须回到 poll-owned virtual XInput current-state 与旧基线时序差异排查。

## ERR-20260615-002

- Logged: 2026-06-15 22:58 CST
- Priority: medium
- Status: open
- Area: tooling / IDA
- Summary: 在 Codex PowerShell 中直接用 IDA 9.3 CLI 查询 `SkyrimSE.exe.i64` 未能产生脚本报告。
- Detail: `idat.exe -A -S... SkyrimSE.exe.i64` 首次超时且残留 `idat` 进程；改窄脚本后 `idat.exe` 和 `ida.exe` 都快速退出但没有生成脚本 report，也没有可靠 stdout/log。已删除本轮临时查询文件；不能把这次命令行尝试当作完成的 IDA 取证。
- Related files: `G:/g/SkyrimSE/SkyrimSE.exe.i64`, `docs/gameplay_ui_owner_code_ida_refactor_plan_zh.md`, `docs/ui_input_ownership_arbitration_plan_zh.md`
- Suggested fix: 后续若要自动化 IDA 查询，先用已知最小脚本验证 IDAPython 启动方式，或直接打开已有 `.i64` 在 GUI/IDA MCP 中查询固定 RVA，再把输出落到 repo-local 诊断文档。

## ERR-20260616-001

- Logged: 2026-06-16 23:19 CST
- Priority: medium
- Status: resolved
- Area: tooling / Codex MCP / IDA
- Summary: Codex 中已配置 IDA MCP，但当前会话无法发现 IDA 工具。
- Detail: `~/.codex/config.toml` 中的 IDA MCP `command` 指向过期的 Microsoft Store Python 版本目录；`ida-pro-mcp` 段还包含当前 `ida_pro_mcp.server.py` 不支持的 `--unsafe` 参数；同时 `service_tier = "default"` 会让 `codex mcp list` 直接报 schema 错误，导致无法验证 MCP 配置。
- Resolution: 将两个 IDA MCP 段的 `command` 改为当前可执行的用户级 Python shim，移除 `--unsafe`，并将 `service_tier` 改为当前 schema 接受的 `flex`。已验证 `ida_pro_mcp` 可 import，`tools/list` 能返回 `decompile`、`disasm`、`xrefs_to`、`py_eval` 等工具，且 `codex mcp list` 显示 `ida-pro-mcp` 与 `ida-pro-mcp-stdio` 均为 enabled。
- Related files: `C:/Users/xuany/.codex/config.toml`

## ERR-20260711-001

- Logged: 2026-07-11 00:40 CST
- Priority: low
- Status: resolved
- Area: tests / xmake target invocation
- Summary: 将两个 focused target 并列传给一次 `xmake build`，第二个 target 被解析为 invalid argument。
- Detail: 当前 xmake CLI 的 build/run 入口每次只接收一个 positional target。多个 focused targets 必须顺序执行 `xmake build -y <target>` / `xmake run -y <target>`，或使用仓库已有 CI 脚本；不能写成 `xmake build -y <target1> <target2>`。
- Related files: `xmake.lua`, `scripts/ci/run_phase8_ci.ps1`
- Resolution: 改为逐 target 构建与运行；失败发生在参数解析阶段，没有产生代码或测试结论。

## ERR-20260711-002

- Logged: 2026-07-11 00:56 CST
- Priority: low
- Status: resolved
- Area: git / remote push race
- Summary: commit 后显式 push 因 remote ref lock expected-old mismatch 被拒，但远端已处于本地目标 commit。
- Detail: `git push` 报远端 ref 已是本地 HEAD、却仍按旧 tracking SHA 做 compare-and-swap。此时不得 force push；先用 `git ls-remote` 对比 local/remote SHA，再 `git fetch` 刷新 tracking ref。SHA 相同表示切片已发布，只是同步竞争导致命令非零。
- Resolution: 已确认 local HEAD、remote branch 均为 `10eb20911789679fb357819f72febcc4afd1c1dc`，fetch 后分支不再 ahead/behind。

## ERR-20260711-003

- Logged: 2026-07-11 01:12 CST
- Priority: low
- Status: resolved
- Area: tests / ingress property stress
- Summary: LatestPadState property fixture 以高于消费速率的真实 digital edge 生产速率运行，误把预期的 ordered-edge backlog/overflow 当成模拟量放大回归。
- Detail: fixture 每 17 个 report 切换一次按键、每 23 个 report 只 drain 1 个 event；即使纯轴 report 完全不入队，digital producer 仍快于 consumer，最终必然填满容量。验证 analog latest-wins 时必须把 event budget 设为足以覆盖真实 edge 速率，并单独断言最大 queue 长度与 edge 数量关系。
- Resolution: 每次 capture 改为 drain 2 个 event，断言最大 pending 不超过初始 UI marker 加两个真实 edge；测试随后通过。

## ERR-20260711-004

- Logged: 2026-07-11 02:33 CST
- Priority: low
- Status: resolved
- Area: Windows build / MSVC PCH memory
- Summary: 公共 presentation/context header 变更触发大范围重编译时，xmake 默认 34 jobs 导致 MSVC `C3859` / `C1076`，Windows 返回 pagefile error 1455。
- Detail: focused ContextResolver target 已先构建并运行通过；随后 `xmake build -y DualPad` 并发重编译大量 PCH translation units 时耗尽 commit/pagefile。该错误是本机并发资源上限，不能当作代码编译结论。
- Resolution: 对大范围 header 变更的本地全量重编译使用 `xmake build -y -j 4 DualPad`；focused targets 仍可使用默认并发。

## ERR-20260711-005

- Logged: 2026-07-11 07:02 CST
- Priority: high
- Status: resolved
- Area: transactional hook patching / exception safety
- Summary: 最后一个 patch site 在“内存已变为 replacement、writer 随后抛异常”时，transaction 误以 `appliedSites == totalSites` 判定 Installed。
- Detail: writer 的返回/异常状态与实际 patch reality 必须分别判断；即使每个 site 最终都短暂呈现 replacement，只要任一 writer 未正常确认，仍必须进入反向 expected-current rollback。该缺陷由 post-write exception 注入触发 Windows fast-fail 的测试断言暴露。
- Resolution: 增加独立 `applicationFailed` 状态；只有全部 compare-write 正常确认且逐站点复验 replacement 才能返回 Installed。post-write exception 现在识别实际已写 site，并恢复全部 original bytes。

## ERR-20260711-006

- Logged: 2026-07-11 07:19 CST
- Priority: medium
- Status: resolved
- Area: CI / release readiness contract drift
- Summary: Phase 8 release gate 仍要求旧的 `_attemptedInstall = true` 源码拼写，误报已经升级为原子尝试与 transactional patch outcome 的 upstream hook。
- Detail: 静态门禁应约束可复述的安全合同，而不是已退休的赋值语句。当前等价且更强的合同由 `_attemptedInstall.exchange(true, ...)`、`ExecutePatchTransaction`、`RolledBack` 和 `UnsafePartial` 共同表达。
- Resolution: 更新 `check_release_readiness.py`，同时要求原子单次 install 尝试和 transactional rollback / unsafe-partial 分支；保留 runtime/version/signature fail-closed 检查。

## ERR-20260711-007

- Logged: 2026-07-11 09:34 CST
- Priority: medium
- Status: resolved
- Area: build / xmake sandbox
- Summary: xmake 配置沙箱没有 Lua 全局 `pcall`，Git 构建来源探测在配置阶段中止。
- Detail: `xmake.lua` 顶层项目描述域不能假设标准 Lua 或脚本域函数可用；本机依次确认 `pcall`、`catch`、`import` 和 `os.iorun` 都不可在该域调用。需要加载模块的逻辑应进入 target `on_load` 脚本域。
- Related files: `xmake.lua`, `src/main.cpp`
- Resolution: 改用 xmake 自带 `devel.git.lastcommit` 模块并 fail closed；发布构建无法解析 Git commit 时不再生成来源不明的 DLL。`main.cpp` 仍保留宏缺失时的 `unknown`，仅服务于不经过 xmake 的独立编译。
