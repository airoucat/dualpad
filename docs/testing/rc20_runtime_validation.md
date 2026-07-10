# RC20 Runtime 验证记录

本文是 `S-DP5-RC20-HOTFIX` 的验证路由与证据边界。所有“通过”只指实际执行过的命令；未执行的实机项不会由 host tests 代替。

## 自动化覆盖

| 合同 | 主要 target |
| --- | --- |
| bounded drain、latest state、ordered edges、overflow recovery、rate matrix | `DualPadIngressTests` |
| runtime owner、immutable output、hook health、pulse integration | `DualPadInputV2Tests` |
| 2/4/8 Poll readers × 100,000 publications | `DualPadInputV2Tests` |
| generation pulse、cancel boundary、Favorites gate | `DualPadNativeButtonCommitTests`、`DualPadGameplayProjectionTests` |
| target-bound refresh、hook operational state | `DualPadPresentationProjectionTests` |
| transactional patch、rollback/tamper/post-write exception | `DualPadRouteHealthContractTests` |
| replay/property/fuzz regression | `DualPadReplayTests`、`DualPadReplayHarnessTests`、`DualPadPropertyTests`、`DualPadFuzzRegressionTests` |

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

以下项目仍未被 host/CI 证明：

- IDA 核对 Skyrim SE 1.5.97 `0xC1AB40 / 0xC1AB9D` caller、call frequency、thread 和生命周期；
- matching build 的 WinDbg/Visual Studio crash dump 与 faulting instruction；
- physical/synthetic DPadUp A/B；
- vanilla Favorites 1000 次 open/close；
- 每个受支持 UI profile 200 次；
- 2-hour soak；
- live/vanilla SWF A/B（条件触发，且 repo 当前不拥有 FavoritesMenu workspace）。

## 当前发布判定

当前为 `NO-GO`。`enable_native_favorites=false` 继续是默认配置。只有动态证据完成且无 blocker 时，才可评估 `GO WITH NATIVE FAVORITES DISABLED`；未完成真实 Favorites 循环和 crash dump 闭环不得给 `GO`。
