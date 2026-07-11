# RC20 Runtime 验证记录

本文是 `S-DP5-RC20-HOTFIX` 的验证路由与证据边界。所有“通过”只指实际执行过的命令；未执行的实机项不会由 host tests 代替。

## 自动化覆盖

| 合同 | 主要 target |
| --- | --- |
| bounded drain、latest state、ordered edges、overflow recovery、rate matrix | `DualPadIngressTests` |
| runtime owner、immutable output、hook health、pulse integration | `DualPadInputV2Tests` |
| menu snapshot event sequence、owner/UI/Scaleform/HUD 隔离 | `DualPadContextResolverTests`、`tests/python/test_runtime_ui_thread_boundary.py` |
| 2/4/8 Poll readers × 100,000 publications | `DualPadInputV2Tests` |
| generation pulse、cancel boundary、Favorites gate | `DualPadNativeButtonCommitTests`、`DualPadGameplayProjectionTests` |
| target-bound refresh、hook operational state | `DualPadPresentationProjectionTests` |
| transactional patch、rollback/tamper/post-write exception | `DualPadRouteHealthContractTests` |
| replay/property/fuzz regression | `DualPadReplayTests`、`DualPadReplayHarnessTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` |
| RC20 实机日志判定 | `tests/python/test_check_rc20_live_log.py` |

## Canonical 命令

```powershell
powershell -ExecutionPolicy Bypass -File scripts/ci/run_phase8_ci.ps1
powershell -ExecutionPolicy Bypass -File scripts/ci/run_rc_readiness.ps1 -ExpectCleanManifest
```

RC readiness 会聚合 Phase8、dispatcher replay diff、builder JSON、reviewed docs、legacy/release/U4 gates、`DualPadDInput8Proxy`、artifact manifest、Graphify 和 diff hygiene。它不把 focused target 包装成新的 canonical 名称。

## 本轮新增 correctness / stress

- 500/1000 Hz producer × 30/60/120 Hz owner × 两种 tie ordering。
- pure analog 零 queue amplification，latest generation 单调且不读取 future state。
- semantic latency P99 不超过一个 producer period。
- 20,000 次 axis publication wall-clock percentile telemetry；当前本机一次记录为 P50/P95/P99 均约 100 ns，仅作趋势样本。
- 2/4/8 concurrent Poll readers，各 100,000 次 publication，无 generation/payload tearing。
- hook 第 2/3 site 失败、expected-current tamper、post-write exception 和 unsafe partial。

## 动态证据门

| 项目 | 状态 | 证据边界 |
| --- | --- | --- |
| IDA `0xC1AB40 / 0xC1AB9D` | 已完成静态调查 | 已确认 identity、bytes、original target、caller graph、ABI 和生命周期；不能证明固定线程或每帧次数 |
| 匹配 build 主菜单启动 | 已通过 | build `1b3ca5a2bc7a` 记录 runtime、hook 地址、`nativeFavorites=false`、owner generation 和单 target refresh |
| 初次读档 owner lifecycle | 已完成并修复 blocker | token 359→360 时 OS thread 迁移；`1edb1949ecc4` 已改为 serialized ticket handoff |
| build `71c57b30ae0a` 安全 smoke | 已执行，发现 blocker | generation 到 3000、1519 次 serialized handoff、无 owner degraded / queue overflow；用户观察到摇杆卡顿、进入菜单后部分按键无响应，gate off 下收藏菜单无法打开；日志有 31 次伪 `sequence_gap` |
| build `b5899084bf6d` 修复复测 | 已执行，仍有 blocker | 44 秒 matching log 无 `sequence_gap` / queue overflow / owner degraded，只有 1 次 `explicit_reset`；用户仍观察到摇杆卡顿与 Journal 扳机翻页无效。该结果证伪“排序/配对修复已覆盖用户主症状” |
| build `4d9a43e845db` current-state 复测 | 部分通过，仍有 blocker | 用户确认摇杆已不卡；Journal L2/R2 仍无效。matching log 达 generation 1800、`nativeFavorites=false`，但缺 clean shutdown marker；live target 为 `Journal Menu` 时 refresh 记录 `uiContext=1`，interaction 仍落在 `ctx=Menu` |
| Poll capped diagnostics | 辅助样本 | 256 组 enter/exit、单线程、`inFlight <= 1`、约 16/17/18 ms；raw log 已被覆盖且缺 build commit，不作为 release proof |
| matching crash dump | 未执行 | 当前没有与本轮 DLL/PDB/log/config/SWF 匹配的 `.dmp` 或 Crash Logger report |
| physical/synthetic DPadUp A/B | 未执行 | 需要用户实机输入与 matching artifact capture |
| Favorites loop / soak | 未执行 | 1000 次 vanilla、每 profile 200 次、2-hour soak 均未完成 |
| live/vanilla SWF A/B | 条件未触发 | 只有 vanilla 正常而 live SWF 崩溃时执行；repo 当前不拥有 FavoritesMenu workspace |

完整静态证据见 [../research/skyrim_xinput_poll_callsite.md](../research/skyrim_xinput_poll_callsite.md)。

build `4d9a43e845db` 的实测把两条症状拆开：用户确认持续模拟量修复已消除摇杆卡顿，但 Journal L2/R2 仍无效。配置中的 `[JournalMenu]` 已正确把 `Axis:LeftTrigger/RightTrigger` 绑定到 `Journal.TabLeft/TabRight`，native descriptor 也正确投影到 LT/RT；断点在更上游的 context classification。Skyrim live name 是 `Journal Menu`，旧 catalog 只把无空格 `JournalMenu` 写入 `menuNameIndex`，而 `ResolveMenuName()` 不读取 alias index，导致实机回退为 `UiContextId::UnknownTrackedMenu / legacy Menu`，`JournalLayer` 从未启用。新增 live-name 端到端测试已在旧目录上正确红灯；补齐 menu name 后，`DualPadContextResolverTests` 和使用 live name 的 `DualPadInputV2Tests` 均通过，仍需 matching build 实机复测。

## 最简手工验证

以下 smoke 不修改配置，`enable_native_favorites=false` 保持安全默认值。它验证摇杆与 fail-closed containment，不等价于 native Favorites crash closure。用户只需执行前 4 步；退出后由维护者运行日志检查器。

1. 从 MO2 启动 SKSE 并读取存档。
2. 连续转动左右摇杆约 30 秒。
3. 打开 Journal 菜单，分别按 L2、R2，确认标签能够向左、向右切换。
4. 返回游戏确认普通按键仍有响应，然后正常退出；维护者运行：

```powershell
python scripts/dev/check_rc20_live_log.py --expect-commit <当前 HEAD> --json
```

检查器验证 build/runtime、native Favorites gate、Poll hook、至少 600 generation、owner degraded、queue overflow / sequence gap、成功的 menu-event UI-task snapshot、单 target refresh、UI task queue failure 与 clean shutdown。`event=rebound` 本身表示串行 ticket 已被接受；不能要求每次 handoff 后都有 `event=tick`，因为 tick 日志只按 600 generation 采样。

用户只需回复「完成」；若肉眼观察到卡顿、无反应或闪退，再补充现象和大约发生时间。日志字段由检查器读取。safe smoke 不要求按收藏键：`enable_native_favorites=false` 时收藏 native route 无响应是预期 containment，不能作为功能通过或失败判断。

## Native Favorites 完整验证

只有在保存 matching DLL/PDB/config/SWF hash、确认 crash logger 可写并准备好立即归档日志后，才临时设置 `enable_native_favorites=true`。先做 1 次 open/close；失败时不要重启游戏，以免覆盖日志或 dump。最小路径通过后再覆盖：

- physical/synthetic DPadUp A/B；
- 快速连按、长按、menu opening 期间再次按、打开后立即关闭；
- loading 后首次打开、设备断连重连后首次打开；
- vanilla Favorites 1000 次 open/close；
- 每个声明支持的 UI profile 200 次；
- 2-hour soak。

每次 pulse 必须恰好一组 down/up，且不能跨 context/epoch。若复现崩溃，必须保留 matching log/dump 并符号化 faulting thread；没有 stack 时不得定位到具体 C++ 语句。

## 当前发布判定

当前为 `NO-GO`。`enable_native_favorites=false` 继续是默认配置。build `4d9a43e845db` 已由用户确认摇杆不卡，但 Journal 扳机翻页仍失败；live `Journal Menu` context classification 修复目前只有 host 端红绿测试，尚未完成 matching build 实机复测。matching dump、Favorites loop 和 soak 也未完成。只有动态证据完成且无 blocker 时，才可评估 `GO WITH NATIVE FAVORITES DISABLED`；未完成真实 Favorites 循环和 crash dump 闭环不得给 `GO`。
