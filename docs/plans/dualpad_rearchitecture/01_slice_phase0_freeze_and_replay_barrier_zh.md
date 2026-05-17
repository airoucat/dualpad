# Phase 0：冻结当前行为并建立 Replay Barrier

## 目标

- 在不改变当前正式主链行为的前提下，为 `HidReader -> PadState -> PadEventSnapshot -> FrameActionPlan -> ActionLifecycleCoordinator -> NativeButtonCommitBackend -> AuthoritativePollState -> UpstreamGamepadHook -> XInputStateBridge` 建立可回放、可比对、可批量回归的 regression barrier。
- 把当前运行时的 4 类外部可观察结果冻结成 golden assets：
  - dispatcher 入口与处理后的 `PadEventSnapshot`
  - `AuthoritativePollFrame`
  - keyboard helper 对 `KeyboardNativeBridge` 的命令流
  - presentation / glyph 兼容面
- 给后续 `Phase 1 / 3 / 6 / 7` 提供统一回归入口；后续切换若改变行为，必须先跑 Phase 0 barrier，再决定是否更新 golden。
- 本 slice 不修旧问题、不顺手优化结构、不提前引入 `InputKernel`。Phase 0 只做“冻结现状并能稳定重放”。
## 当前 repo reality 缺口与 breach 边界

- 在 `Phase 0` 真正开工前，当前 repo 允许尚不存在下列 deliverables：
  - `src/input_v2/telemetry/`
  - `scripts/dev/dualpad_trace_diff.py`
  - `target("DualPadReplayHarness")`
  - `target("DualPadReplayHarnessTests")`
  - `tests/replay/golden/phase0/`
- 这些缺口是本 slice 要新增并落地的实现面，不是 baseline current truth 已兑现的现状承诺。
- 审查时如果 bundle 或当前 worktree 如实呈现“这些入口现在还不存在”，应判定为实现前缺口，而不是 bundle 漏打或 authority 断裂。
- 只有在下面任一情况发生时，缺失才构成正式 breach：
  - `.dualpad-builder/` 已把 `Phase 0` 标成 done / promoted / 允许下游 slice 直接依赖
  - `Phase 0` 的 prove-out、exit gate 或 handoff 被宣称已经通过
  - 文档、脚本或 CI 把这些入口表述成“当前 repo reality 已存在的默认 gate”

## 冻结的设计决定

1. **输入冻结边界固定在 `PadEventSnapshotDispatcher` 之前和之后两层。**
   - 必录 `SubmitSnapshot(...)` 收到的 ingress snapshot。
   - 必录 `DrainOnMainThread(...)` 之后实际交给 `PadEventSnapshotProcessor::Process(...)` 的 processed snapshot。
   - 必录 dispatcher 的 drain 时序；`Phase 7` 不能事后再猜当时是如何 coalesce 的。

2. **Replay 资产格式固定为 repo 内 CSV bundle，不引入新的运行时解析依赖。**
   - replay 资产唯一根路径固定为 `tests/replay/golden/`
   - Phase 0 golden 目录固定为 `tests/replay/golden/phase0/<scenario>/`
   - harness 输出目录固定为 `build/replay/<scenario>/`
   - diff 报告目录固定为 `build/replay-diff/<scenario>/`
   - Phase 0 不为 trace 资产引入 JSON 库；schema 以固定列 CSV 落地，便于 C++ 和 Python 同时消费。

3. **新增 Phase 0 代码固定落在 `src/input_v2/telemetry/`，不把 recorder / replay 逻辑塞回旧主线模块。**
   - 新建：
     - `src/input_v2/telemetry/TraceSchema.h`
     - `src/input_v2/telemetry/TraceSchema.cpp`
     - `src/input_v2/telemetry/InputTraceRecorder.h`
     - `src/input_v2/telemetry/InputTraceRecorder.cpp`
     - `src/input_v2/telemetry/ReplayHarness.h`
     - `src/input_v2/telemetry/ReplayHarness.cpp`
   - 旧模块只允许添加被动采样点和最小调试快照接口，不允许把 replay 逻辑写进它们内部。

4. **当前行为中的“已知不优雅但已在运行”的语义一律冻结，不在 Phase 0 修。**
   - `ScaleformGlyphBridge` 里 `ParseInputContextName(...).value_or(InputContext::Menu)` 的 fallback 视为当前真相，先录下来。
   - `PadEventSnapshotDispatcher` 当前 `kPendingSnapshotCapacity=256`、`kDefaultDrainBudget=16`、`kTaskDrainBudget=64`、`kUpstreamTaskFallbackHighWatermark=128`、`kUpstreamTaskFallbackPollStaleMs=250` 视为当前 dispatcher 合同，先录下来。
   - `GameplayOwnershipCoordinator::GameplayPresentationState` 与 `InputModalityTracker` 当前兼容面读法视为当前真相，先录下来。
   - 如果录 trace 时发现现状有 bug，Phase 0 的职责是把 bug 录成 golden，并在 diff 报告里可见，不是在这一 slice 偷改行为。

5. **keyboard helper 冻结边界固定在 `KeyboardNativeBridge::EnqueueCommand(...)`。**
   - 不以 `dinput8` 外部消费者为冻结边界。
   - golden 记录命令类型、顺序、scancode 和来源 action，后续任何 helper 改造都必须先与这一层对齐。

6. **presentation 兼容面冻结为一个显式快照结构，而不是散落日志。**
   - 在 `src/input/InputModalityTracker.*` 新增只读调试接口 `CaptureCompatibilitySurfaceForReplay()`。
   - 快照字段固定为：
     - `context`
     - `context_epoch`
     - `is_using_gamepad`
     - `gamepad_controls_cursor`
     - `gamepad_device_enabled`
     - `presentation_owner`
     - `cursor_owner`
     - `gameplay_engine_owner`
     - `gameplay_menu_entry_owner`
   - 这个接口只能读现有状态，不得新发明判断逻辑。

7. **trace schema 版本从 Phase 0 就要锁定。**
   - 在 `src/input_v2/telemetry/TraceSchema.h` 定义 `inline constexpr std::uint32_t kTraceSchemaVersion = 1;`
   - 后续任何列变更都必须显式 bump schema version，并同步更新 `scripts/dev/dualpad_trace_diff.py` 的 migrator；不能在后续 slice 里静默改列。

## 前置依赖

- 先读这些当前 truth：
  - `docs/authoritative-baseline/README.md`
  - `docs/authoritative-baseline/work-packages/README.md`
  - `.dualpad-builder/feature_list.json`
  - `.dualpad-builder/sprint_plan.json`
  - `docs/current_input_pipeline_zh.md`
  - `docs/mapping_snapshot_atomicity_audit_and_injection_contract_zh.md`
  - `docs/main_menu_glyph_current_status_zh.md`
  - `src/ARCHITECTURE.md`
- 如需理解 gameplay/menu 历史分层背景，可额外回看 `docs/gameplay_input_ownership_investigation_and_plan_zh.md`。
- 如需历史背景或比对拆分前总纲，再额外回看 `docs/plans/dualpad_rearchitecture/README_zh.md` 与 `dualpad_rearchitecture_plan_zh.md`；它们不是本 slice 的 current truth 输入。
- 实现本 slice 前，先在 `.dualpad-builder/progress.md` 追加一条 “Phase 0 start” 记录；结束时再追加 “Phase 0 done / blocked” 记录。
- `Phase 0` 开工前，必须先完成 builder memory 里登记的 `DP1a Route-health contract freeze` 与 `DP4a Glyph compat diagnostics freeze`；不得在 replay barrier 里临时拍板 route hierarchy 或 glyph diagnostics exposure layer。
- 当前 repo 已存在 focused tests：`DualPadMenuContextPolicyTests`、`DualPadRouteHealthContractTests`、`DualPadGlyphResolutionCompatTests`；但仍没有独立 replay harness target。Phase 0 必须新增 replay target，不能把这些 focused tests 当成 replay harness 替代。
- `keyboard helper` 相关场景要求本机能跑：
  - `xmake build DualPad`
  - `xmake build DualPadDInput8Proxy`
- 机器私有的 live capture 路径如需绝对路径覆盖，只写进 `AGENTS.win.md`；共享文档里只使用 repo-relative 路径。

## 涉及代码与文档

### 现有代码触点

- `src/input/injection/PadEventSnapshot.h`
- `src/input/injection/PadEventSnapshotDispatcher.h`
- `src/input/injection/PadEventSnapshotDispatcher.cpp`
- `src/input/injection/PadEventSnapshotProcessor.h`
- `src/input/injection/PadEventSnapshotProcessor.cpp`
- `src/input/AuthoritativePollState.h`
- `src/input/AuthoritativePollState.cpp`
- `src/input/backend/NativeButtonCommitBackend.h`
- `src/input/backend/NativeButtonCommitBackend.cpp`
- `src/input/backend/KeyboardHelperBackend.h`
- `src/input/backend/KeyboardHelperBackend.cpp`
- `src/input/backend/KeyboardNativeBridge.h`
- `src/input/backend/KeyboardNativeBridge.cpp`
- `src/input/InputModalityTracker.h`
- `src/input/InputModalityTracker.cpp`
- `src/input/injection/GameplayOwnershipCoordinator.h`
- `src/input/injection/GameplayOwnershipCoordinator.cpp`
- `src/input/glyph/ScaleformGlyphBridge.h`
- `src/input/glyph/ScaleformGlyphBridge.cpp`
- `src/input/RuntimeConfig.h`
- `src/input/RuntimeConfig.cpp`
- `config/DualPadDebug.ini`
- `xmake.lua`

### 新增代码与脚本落点

- `src/input_v2/telemetry/TraceSchema.h`
- `src/input_v2/telemetry/TraceSchema.cpp`
- `src/input_v2/telemetry/InputTraceRecorder.h`
- `src/input_v2/telemetry/InputTraceRecorder.cpp`
- `src/input_v2/telemetry/ReplayHarness.h`
- `src/input_v2/telemetry/ReplayHarness.cpp`
- `tests/ReplayHarnessTests.cpp`
- `tests/replay/golden/`
- `scripts/dev/dualpad_trace_diff.py`

### 本 slice 需要维护的文档入口

- `docs/plans/dualpad_rearchitecture/README_zh.md`
- `.dualpad-builder/progress.md`

## 实施步骤

1. **先冻结 trace schema、目录布局和开关，不先写 recorder。**
   - 在 `src/input_v2/telemetry/TraceSchema.*` 定义 Phase 0 的固定 bundle：

   ```text
   tests/replay/golden/phase0/<scenario>/
     dispatcher_schedule.csv
     ingress_snapshot_frames.csv
     ingress_snapshot_events.csv
     processed_snapshot_frames.csv
     processed_snapshot_events.csv
     expected_authoritative_poll.csv
     expected_keyboard_bridge.csv
     expected_presentation_surface.csv
     glyph_queries.csv
     expected_glyph_results.csv
   ```

   - 列定义在这一阶段就写死：
     - `dispatcher_schedule.csv`
       - `step_index,op,sequence,budget,reason,route_state,last_poll_age_ms,hook_installed,pending_before,pending_after,drained_count`
       - `op` 只允许 `submit` / `drain`
       - `reason` 只允许 `upstream_poll` / `frame_pump_assist_stale` / `task_fallback_high_water` / `frame_pump_disabled`
       - `route_state` 只允许 `active_fresh` / `active_stale` / `disabled`
       - `last_poll_age_ms` 为空时固定写 `none`
     - `ingress_snapshot_frames.csv` 与 `processed_snapshot_frames.csv`
       - `sequence,first_sequence,source_timestamp_us,context,context_epoch,overflowed,coalesced,cross_context_mismatch,digital_mask,left_stick_x,left_stick_y,right_stick_x,right_stick_y,left_trigger,right_trigger`
     - `ingress_snapshot_events.csv` 与 `processed_snapshot_events.csv`
       - `sequence,event_index,type,trigger_type,code,modifier_mask,axis,previous_value,value,timestamp_us,touch_id,touch_x,touch_y,touchpad_mode,touch_region,slide_direction`
     - `expected_authoritative_poll.csv`
       - `poll_sequence,context,context_epoch,source_timestamp_us,down_mask,pressed_mask,released_mask,pulse_mask,unmanaged_down_mask,unmanaged_pressed_mask,unmanaged_released_mask,unmanaged_pulse_mask,managed_mask,committed_down_mask,committed_pressed_mask,committed_released_mask,move_x,move_y,look_x,look_y,left_trigger,right_trigger,has_digital,has_analog,overflowed,coalesced`
     - `expected_keyboard_bridge.csv`
       - `sequence,command_index,command_type,scancode,action_id,contract,context`
     - `expected_presentation_surface.csv`
       - `sequence,context,context_epoch,is_using_gamepad,gamepad_controls_cursor,gamepad_device_enabled,presentation_owner,cursor_owner,gameplay_engine_owner,gameplay_menu_entry_owner`
     - `glyph_queries.csv`
       - `query_id,sequence,action_id,context_name`
     - `expected_glyph_results.csv`
       - `query_id,ok,button_art_token,semantic_id,context_name`
   - 在 `src/input/RuntimeConfig.*` 和 `config/DualPadDebug.ini` 新增 `[Replay]` 段，字段固定为：
     - `enable_trace_recording = false`
     - `trace_output_dir = build/replay-captures`
     - `trace_session = default`
     - `trace_record_glyph_queries = true`
   - 这一阶段不碰业务逻辑，只把 schema 和 runtime gate 定住。

2. **实现被动 recorder，只允许采样，不允许改流。**
   - 在 `src/input_v2/telemetry/InputTraceRecorder.*` 实现单例 recorder，默认关闭，开启后按 `trace_output_dir/trace_session` 输出 CSV bundle。
   - recorder 必须只做追加写，不得反向调用 gameplay / UI 逻辑。
   - 采样点固定如下：
     - `PadEventSnapshotDispatcher::SubmitSnapshot(...)`
       - 记录 `dispatcher_schedule.csv` 的 `submit`
       - 记录 `ingress_snapshot_frames.csv`
       - 记录 `ingress_snapshot_events.csv`
     - `PadEventSnapshotDispatcher::DrainOnMainThread(...)`
       - 每次进入时记录 `dispatcher_schedule.csv` 的 `drain`
     - `PadEventSnapshotProcessor::Process(...)`
       - 在 `PublishUnmanagedDigitalState(...)` 之后记录：
         - `processed_snapshot_frames.csv`
         - `processed_snapshot_events.csv`
         - `expected_authoritative_poll.csv`
         - `expected_presentation_surface.csv`
   - `expected_authoritative_poll.csv` 的 source 一律用 `AuthoritativePollState::ReadSnapshot()`，不要再拼第二份 poll state。
   - `expected_presentation_surface.csv` 必须来自：
     - `InputModalityTracker::CaptureCompatibilitySurfaceForReplay()`
     - `GameplayOwnershipCoordinator::GetPublishedGameplayPresentationState()`
   - `InputModalityTracker` 如需新增 `CaptureCompatibilitySurfaceForReplay()`，只做只读快照，不改变 owner 决策路径。

3. **把 keyboard helper 和 glyph 兼容面也纳入 recorder。**
   - `expected_keyboard_bridge.csv` 的唯一记录点固定在 `src/input/backend/KeyboardNativeBridge.cpp` 的 `EnqueueCommand(...)`。
   - 记录字段：
     - processed snapshot `sequence`
     - 同一 snapshot 内的 `command_index`
     - `command_type`
     - `scancode`
     - 来源 `action_id`
     - `contract`
     - `context`
   - 为了拿到 `action_id / contract / context`，在 `KeyboardHelperBackend` 到 `KeyboardNativeBridge` 之间补一层轻量 replay metadata 传递；不要把这些信息重新从日志反推。
   - glyph 记录点固定在：
     - `ScaleformGlyphBridge::HandleGetActionGlyphToken(...)`
     - `ScaleformGlyphBridge::HandleGetActionGlyph(...)`
   - `glyph_queries.csv` 和 `expected_glyph_results.csv` 都记录规范化结果，不直接序列化 `GFxValue`。
   - 规范化规则固定为：
     - token API 只落 `ok=false/true + button_art_token`
     - descriptor API 只落 `ok, button_art_token, semantic_id, context_name`
   - `DP4a` 期间新增的 `status / fallback / ambiguity` 只允许进入日志、trace 或 targeted tests；在 `Phase 6` 之前，不得扩到 `expected_glyph_results.csv` 或旧 SWF 返回对象。
   - 这一阶段保留当前 fallback 行为；invalid `contextName` 仍按现状录成 `Menu` fallback 结果。

4. **实现 replay harness，先保证“同 bundle 可重放”，再谈批量。**
   - 在 `src/input_v2/telemetry/ReplayHarness.*` 实现两个固定模式：
     - `dispatcher`：按 `dispatcher_schedule.csv` 回放 ingress snapshot，并驱动 `PadEventSnapshotDispatcher`
     - `processor`：直接按 `processed_snapshot_*.csv` 喂 `PadEventSnapshotProcessor`
   - 两个模式都要在每个 scenario 开头统一 reset：
     - `PadEventSnapshotProcessor::ResetState()`
     - `PadEventSnapshotDispatcher` 队列清空
     - `AuthoritativePollState::Reset()`
     - `NativeButtonCommitBackend::Reset()`
     - `KeyboardHelperBackend::Reset()`
     - `GameplayOwnershipCoordinator::Reset()`
   - replay harness 输出目录固定为 `build/replay/<scenario>/`，输出文件名与 golden bundle 同名。
   - harness 不是测试断言层；它只负责“重放并产出 candidate bundle”。

5. **把 replay runner 和测试入口写成明确 target，不复用现有 `MenuContextPolicyTests`。**
   - 在 `xmake.lua` 新增：
     - `target("DualPadReplayHarness")`
       - `set_kind("binary")`
       - 负责读取 scenario、输出 candidate bundle
     - `target("DualPadReplayHarnessTests")`
       - `set_kind("binary")`
       - 编译 `tests/ReplayHarnessTests.cpp`
   - `tests/ReplayHarnessTests.cpp` 必须至少覆盖：
     - CSV schema round-trip
     - `dispatcher_schedule.csv` 驱动下的 submit/drain 顺序恢复
     - `processed_snapshot` 回放能还原 `expected_authoritative_poll.csv`
     - glyph 规范化结果能还原 `expected_glyph_results.csv`
     - `KeyboardNativeBridge` 命令顺序保持稳定
   - 这一步完成后，Phase 0 的最小命令入口固定为：
     - `xmake build DualPadReplayHarness`
     - `xmake build DualPadReplayHarnessTests`
     - `xmake run DualPadReplayHarnessTests`

6. **实现 diff 工具，比较规则现在就定死，不留给后面决定。**
   - diff 工具固定落在 `scripts/dev/dualpad_trace_diff.py`。
   - 它接收：
     - `--expected tests/replay/golden/phase0/<scenario>`
     - `--actual build/replay/<scenario>`
     - 可选 `--report build/replay-diff/<scenario>/report.md`
   - 比较规则固定为：
     - 行数必须完全一致
     - 除浮点列外，其余列逐字段精确匹配
     - 浮点列容差固定为 `1e-4`
     - 字段顺序是合同的一部分，不做列名重排
   - 报告输出至少要给：
     - 首个 diff 的文件名
     - 行号
     - 期望值 / 实际值
     - 对应 scenario 和 mode
   - 同时支持 batch 模式：
     - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`

7. **golden trace 范围在本 slice 一次性定死，不把“先录哪些”留到 work 阶段。**
   - Phase 0 默认必录 10 个 repo-owned scenario 目录，另保留 1 个 `FavoritesMenu` 条件场景。
   - `FavoritesMenu` 条件场景不得在缺少页面源码 / SWF patch workspace / artifact inventory 时作为 Phase 0 退出条件；如果本轮决定启用它，必须先恢复对应 workspace，并在 `.dualpad-builder/progress.md` 记录可复述的 artifact 来源与验证边界。

   | scenario 目录 | 采集方式 | 必须覆盖的 surface |
   | --- | --- | --- |
   | `01_gameplay_walk_attack_block_sprint` | live capture | poll / presentation / keyboard(empty) |
   | `02_gameplay_menu_roundtrip` | live capture | dispatcher / poll / presentation |
   | `03_main_menu_glyph` | live capture | glyph / presentation |
   | `04_journal_confirm_cancel` | live capture | glyph / poll / presentation |
   | `05_map_cursor_zoom_open_journal` | live capture | glyph / presentation / poll |
   | `06_favorites_page_lr_accept_cancel` | conditional live capture；仅在恢复 `FavoritesMenu` workspace 后启用 | poll / presentation；glyph 只在页面源码与 patch workspace 已恢复时要求 |
   | `07_book_page_lr` | live capture | glyph / presentation |
   | `08_console_creations_lockpicking` | live capture，可拆 3 条 session 后合并 | poll / presentation / glyph(if any) |
   | `09_combo_native_pause_screenshot_hotkeys` | live capture + `DualPadDInput8Proxy` | keyboard bridge / poll |
   | `10_backlog_gap_overflow` | synthetic fixture，直接写 golden bundle | dispatcher / processed snapshot / poll |
   | `11_config_reload_success_failure` | synthetic fixture + temp ini | replay parser / runtime config surface |

   - `08_console_creations_lockpicking` 允许内部拆成：
     - `console`
     - `creations`
     - `lockpicking`
     但最终 check-in 目录仍合并到一个 scenario bundle，不能把覆盖面丢给后续 slice。
   - `10_backlog_gap_overflow` 必须显式覆盖：
     - queue overflow
     - same-context gap
     - cross-context mismatch
     - coalesced degraded delivery
   - `11_config_reload_success_failure` 必须显式覆盖：
     - `RuntimeConfig::Reload()` 成功
     - `RuntimeConfig::Reload()` 失败后保留默认/last-known-good 行为
     - `BindingConfig::Reload()` 成功 / 失败
     - `MenuContextPolicy::Reload()` 成功 / 失败

8. **最后把验证命令和 close-out 固定下来。**
   - 本 slice 完成前必须跑：
     - `xmake build DualPad`
     - `xmake build DualPadDInput8Proxy`
     - `xmake build DualPadReplayHarness`
     - `xmake build DualPadReplayHarnessTests`
     - `xmake run DualPadReplayHarnessTests`
   - 对每个 scenario 至少跑一遍：

   ```powershell
   xmake run DualPadReplayHarness -- --scenario tests/replay/golden/phase0/01_gameplay_walk_attack_block_sprint --mode dispatcher --output build/replay/01_gameplay_walk_attack_block_sprint
   python scripts/dev/dualpad_trace_diff.py --expected tests/replay/golden/phase0/01_gameplay_walk_attack_block_sprint --actual build/replay/01_gameplay_walk_attack_block_sprint --report build/replay-diff/01_gameplay_walk_attack_block_sprint/report.md
   ```

   - 全量批量命令固定为：

   ```powershell
   python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff
   ```

   - 代码文件有改动，所以 close-out 前必须执行：
     - `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## Replay Extension Governance

- replay 资产的唯一 canonical root 固定为 `tests/replay/golden/`；后续 slice 不得另起 `tests/golden/replay/`、`replay_golden/` 或其他平行根目录。
- `Phase 0` 只拥有 `tests/replay/golden/phase0/` 命名空间；后续能够扩 replay 的 slice 只允许是：
  - `Phase 6`
    - 新增 `tests/replay/golden/phase6_prompt/<scenario>/`
    - 只允许承载 prompt 相关扩展 surface，例如 `prompt_scope.csv`、`prompt_descriptors.csv`、`prompt_queries.csv`
  - `Phase 7`
    - 新增 `tests/replay/golden/phase7_ingress/<scenario>/`
    - 只允许承载 ingress / transition / recovery 扩展 surface，例如 `fact_frames.csv`、`transition_frames.csv`、`recovery_events.csv`
  - `Phase 8`
    - 只允许把既有 namespace 接进 `DualPadReplayTests`、`scripts/ci/run_phase8_ci.ps1` 与 `.github/workflows/dualpad-ci.yml`
    - `DualPadReplayHarnessTests` 只属于 Phase 0 bootstrap；进入 `Phase 8` 前，它的断言与 batch runner 必须并入 `DualPadReplayTests`
    - `Phase 8` 的最终默认 CI target 矩阵固定为：
      - `DualPadReplayTests`
      - `DualPadInputV2Tests`
      - `DualPadIngressTests`
      - `DualPadPromptSnapshotTests`
      - `DualPadPropertyTests`
      - `DualPadFuzzRegressionTests`
    - 不允许再定义新的 replay schema family，也不允许重定根路径
- 目录与命名规则现在写死：
  - namespace 固定命名为 `phaseN_<topic>`
  - namespace 下仍按 `<scenario>/` 组织
  - 若扩展场景与 `phase0` 语义同源，目录名必须复用 `phase0` 的 scenario 名
  - synthetic 场景固定加前缀 `synthetic_`
  - extension 文件名只能描述新增 surface，不允许重命名、覆盖或替代 `phase0` 已冻结文件
- schema / migrator 责任固定如下：
  - 任何 slice 只要新增 replay 文件或列，就必须在同一 slice 内同时完成：
    - bump `kTraceSchemaVersion`
    - 更新 `scripts/dev/dualpad_trace_diff.py` migrator
    - 更新 `ReplayHarness` 解析入口与 diff 映射
    - 补齐对应 namespace 的 golden 目录与最小验证样例
  - 不允许只提交新 golden 文件，而把 parser / migrator / diff 更新留给后续 slice
- CI / 接线责任固定如下：
  - `Phase 6` 与 `Phase 7` 若引入 replay extension，必须在本 slice 内补本地 runner、测试入口和验证命令
  - `Phase 8` 只负责把 `tests/replay/golden/` 下已经冻结的 namespace 接入默认 CI，并接线到上面那 6 个 canonical targets；不替前面 slice 回填 schema 或命名规则
  - `Phase 8` 不得把 `DualPadInputV2Tests`、`DualPadIngressTests` 或 `DualPadPromptSnapshotTests` 静默移出默认 CI，也不得继续保留 `DualPadReplayHarnessTests` 作为独立默认 gate
- `tests/replay/golden/phase0/` 始终是第一层回归护栏；`phase6_prompt/` 与 `phase7_ingress/` 只能追加更细的比较面，不能降低或绕过 `phase0/` 的回归要求

## 验证与观测

- **构建验证**
  - `xmake build DualPad`
  - `xmake build DualPadDInput8Proxy`
  - `xmake build DualPadReplayHarness`
  - `xmake build DualPadReplayHarnessTests`
- **自动验证**
  - `xmake run DualPadReplayHarnessTests`
  - `python scripts/dev/dualpad_trace_diff.py --batch tests/replay/golden/phase0 --actual-root build/replay --report-root build/replay-diff`
- **runtime 观测点**
  - `PadEventSnapshotDispatcher` 的 submit/drain/coalesce 日志
  - `PadEventSnapshotProcessor` 的 degraded recovery 日志
  - `KeyboardHelperBackend` / `KeyboardNativeBridge` 的 helper 命令日志
  - `ScaleformGlyphBridge` 的 glyph query 日志
  - `InputModalityTracker` / `GameplayOwnershipCoordinator` 的 presentation 变化日志
- **人工 spot check 最低要求**
  - `03_main_menu_glyph`：`DualPad_GetActionGlyphToken` 与 `DualPad_GetActionGlyph` 的同 action/query 结果一致
  - `05_map_cursor_zoom_open_journal`：cursor ownership 与 `GamepadControlsCursor` 变化被 recorder 记录
  - `09_combo_native_pause_screenshot_hotkeys`：`expected_keyboard_bridge.csv` 里必须能看到 `pause / screenshot / hotkey3-8` 的命令顺序

## 退出条件

- `src/input_v2/telemetry/` 下的 recorder、schema、harness 文件全部落地，并且 `kTraceSchemaVersion = 1` 已固定。
- `scripts/dev/dualpad_trace_diff.py` 可单场景、可 batch 运行。
- `xmake.lua` 里已有 `DualPadReplayHarness` 和 `DualPadReplayHarnessTests` 两个独立 target。
- `tests/replay/golden/phase0/` 下 10 个 repo-owned mandatory scenario 目录全部存在，且 bundle 文件齐全；允许某些 CSV 只有表头，但不允许缺文件。
- 若启用 `06_favorites_page_lr_accept_cancel`，对应 workspace / artifact inventory / capture surface 已先写入 `.dualpad-builder/progress.md`，且该场景 bundle 文件齐全；未恢复 workspace 时不得把 Favorites glyph capture 当作 Phase 0 breach 或退出条件。
- replay candidate 对 golden 的 diff 为 0；若有差异，必须先解释再改 golden，不能静默覆盖。
- recorder 在 `enable_trace_recording = false` 时不改变现有运行时行为；这必须通过至少一次关闭开关的 live smoke 验证确认。
- `.dualpad-builder/progress.md` 已记录开始、完成和验证命令结果。

## 交接给下一 slice 的合同

- `Phase 1` 及之后的所有 slice，都必须把 `tests/replay/golden/phase0/` 视为 canonical replay root `tests/replay/golden/` 下的第一层回归护栏；新方案进入 mainline 前先跑 Phase 0 barrier。
- 下一个 slice 只能依赖这些稳定产物，不能要求 Phase 0 再返工 schema：
  - `dispatcher_schedule.csv`
  - `ingress_snapshot_frames.csv`
  - `ingress_snapshot_events.csv`
  - `processed_snapshot_frames.csv`
  - `processed_snapshot_events.csv`
  - `expected_authoritative_poll.csv`
  - `expected_keyboard_bridge.csv`
  - `expected_presentation_surface.csv`
  - `glyph_queries.csv`
  - `expected_glyph_results.csv`
- `DualPadReplayHarness` 的 CLI 入口和 `scripts/dev/dualpad_trace_diff.py` 的参数名在 `Phase 1` 前不能再改；如果必须改，先 bump schema version，再提供兼容层。
- 后续 slice 若新增比较面，只能按 `Replay Extension Governance` 追加 namespace、新文件或新列，不能删除 Phase 0 已冻结的列，也不能改 replay root。
- `Phase 1` 的第一步不是直接改 manifest/compiler，而是先把它的改动跑过 Phase 0 barrier，确认当前行为仍被完整解释。
