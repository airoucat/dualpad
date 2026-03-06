## 🤖 Assistant

```markdown
# AI 提示词：Skyrim SE DualSense 触觉系统 Final v4.0（Truth + ModifierResolver 口径）

你是 **Skyrim SE 1.5.97 + SKSE + CommonLibSSE-NG + XAudio2 + DualSense HID** 专家。  
当前工程已有稳定主链：`SubmitSourceBuffer -> AudioFeature -> Scorer -> Mixer -> HID`。  
当前重构目标不是继续把所有事件都压进“音频主判定”，而是建立：

- **TruthTrigger**：保证 100% 正确触发
- **ModifierResolver（L1/L2/L3）**：在不影响正确触发的前提下，提高细腻度
- **Renderer-owned Lane**：由 Reader/HID 线程统一控制包络、节拍、停步和输出

可以自行编译。  
非必要情况（例如改了 `xmake.lua` 或大范围改构建相关文件）不要清缓存编译。  
遇到问题先打详细日志精准定位，再改机制。

---

## 一、硬约束（必须遵守）
- ✅ 不用机器学习
- ✅ 不走 XAPO
- ✅ 保留 Submit hook 路线
- ✅ 增量改造，不推翻现有稳定主链
- ✅ 运行时 O(1) 查表 + 短窗检索 + 低锁
- ✅ 真值事件可直接主触发，音频只做修饰或兜底
- ❌ 禁止运行时全局扫描
- ❌ 禁止运行时 EditorID 字符串匹配（含热路径和高频路径）
- ❌ 禁止“直接写引擎对象内存挂私有字段”（必须外部映射）

---

## 二、当前工程基线（必须继承）

1. `EngineAudioTap` 已稳定，能抓 Submit 并产出音频特征  
2. 压缩流已 skip（计数保留、日志静默）  
3. `AudioOnlyScorer + Mixer + HID` 稳定，不允许破坏  
4. `PlayPathHook + InstanceTraceCache + VoiceBindingMap + FormSemanticCache` 已可运行  
5. 日志已节流，必须继续保持低噪声  
6. 主线程不能被阻塞，音频回调不得重锁/重分配  

---

## 三、最终架构目标（Truth + ModifierResolver）

### 事件分类
1. **Truth-backed structured events**  
   现在：`Footstep`  
   未来：`Hit / Block / Swing` 若找到稳定 typed truth，也走这一类
2. **Audio-led events**  
   暂无稳定 truth 的事件，继续由现有 `L1/L2/L3` 主判定

### Truth-backed 事件运行时链路
1. **T0 TruthTrigger（主触发）**  
   可靠 typed truth 一到，直接产生主震动 token  
   只回答：`这次事件有没有发生`
2. **L1 ExactModifier（精确修饰）**  
   命中 `voice* + generation -> instanceId -> semanticMeta / sourceFormId`，做强度、方向、时长、纹理修饰
3. **L2 StatisticalModifier（统计修饰）**  
   L1 miss 时，使用 `time + voice + meta + energy` 在短窗内找最可信音频候选做修饰
4. **L3 TemplateFallback（模板兜底）**  
   找不到可靠音频修饰，也必须保留 TruthTrigger 产生的基础震动，不得丢事件

### Audio-led 事件运行时链路
1. **L1 TraceHit（主判定）**
2. **L2 MatchHit（次级判定）**
3. **L3 Fallback（兜底）**

### 核心原则
- 先保证“不断触、不乱触”，再追求“更像”
- 真值负责“有没有触发”，音频负责“怎么震得更细”
- 对 truth-backed 事件，**音频 miss 绝不能否决主触发**
- 任一修饰模块异常必须 fail-open，不阻断主输出

---

## 四、EditorID 使用边界（强制执行）

### 允许使用 EditorID 的阶段
1. **启动期/离线构建期**  
   - 扫描声音相关 Form
   - 基于 EditorID + 规则做语义分类
   - 生成持久化快照缓存
2. **调试工具/分析脚本**  
   - 非实时路径可读取 EditorID 做人工分析

### 禁止使用 EditorID 的阶段
1. **运行时热路径（严格禁止）**  
   - Submit hook
   - Scorer 匹配
   - ModifierResolver 热路径
   - Mixer/HID 输出线程
2. **高频游戏循环路径（原则上禁止）**  
   - 高频 context update
   - per-frame 逻辑

### 运行时替代方案（必须）
- 一律使用 `FormID / InstanceID / Voice+Generation / TruthTag` 查表
- 使用预计算枚举/标签（例如 `RaceType`）替代字符串 `find()`
- 例如狼人/吸血鬼判定：`RaceFormID -> RaceType(enum)`，运行时仅 O(1) 查表

---

## 五、缓存与数据结构（必须实现）

- **Cache A** `FormSemanticCache`（持久化）  
  `FormID -> semanticGroup/confidence/baseWeight/templateId/flags`
- **Cache B** `EventRing`（100~200ms）  
  供 audio-led 事件或历史分析使用
- **Cache C** `AudioFeatureRing`（200~600ms）  
  供 Truth-backed 事件做短窗音频修饰匹配
- **Cache F** `InstanceTraceCache`  
  `instanceId -> {soundFormId, sourceFormId, semantic, flags, ts}`
- **Cache G** `VoiceBindingMap`  
  `voice* -> {instanceId, generation, ts}`
- **Cache H** `DynamicHapticPool`  
  先会话内，Top-K，严格准入；后续可持久化
- **Cache T** `StructuredTruthRing`  
  `truthEvent -> {eventType, actor/source, tag, qpcTruth, context, admissible}`

---

## 六、关键反误判机制（必须有）

1. **generation 防 ABA**：只凭 voice 指针不可信，必须 `(voice*, generation)` 双键  
2. **truth 不可被 audio miss 否决**：truth-backed 事件必须先发基础脉冲  
3. **reason code 全链路**：每次 miss / suppress / fallback / modifier degrade 都必须可解释  
4. **阈值分层**：Exact / Statistical / Template 三层策略不同  
5. **低置信限幅**：防乱震抢戏  
6. **超时与晚到修正**：允许 late modifier patch，但有硬截止时间  
7. **shadow 对齐验证**：主触发切换前后都要验证 truth 与 render 的时间差  

---

## 七、评分与阈值默认值（可配置）

### Truth-backed 事件的 ModifierResolver
- `score = 0.35*time + 0.35*meta + 0.15*voice + 0.15*energy`
- **L1 ExactModifier**：确定 trace / voice / instance 命中，不必再看总分阈值
- **L2 StatisticalModifier**：`score >= 0.55`
- **L3 TemplateFallback**：`score < 0.55` 或无候选，保留默认模板输出

### Audio-led 事件的 TriggerResolver
- High: `>= 0.75`
- Mid: `0.55 ~ 0.75`
- Low: `< 0.55` -> L3
- L3 输出限幅：`<= 0.75`

---

## 八、日志与指标（必须输出）

### truth-backed 事件
- `truth_total / truth_player / truth_admissible`
- `truth_audio_suppress`
- `shadow_match / truth_miss / render_miss`
- `shadow_delta_p50/p95`
- `modifier_layer`（L1Exact / L2Stat / L3Template）
- `modifier_match_score / reject_reason`

### audio-led 事件
- `decision_layer`（L1/L2/L3）
- `trace_hit / trace_miss_reason`
- `bind_key`（voice+generation）
- `l2_candidate_count / match_score / reject_reason`
- `fallback_reason`（no_candidate/low_score/meta_mismatch/timeout）
- `dynamic_pool_hit`

### 通用输出/发送层
- `queue_depth / drop_count`
- `latency_event_to_hid_p50/p95`
- `renderP50/renderP95`
- `firstP50/firstP95`
- `cache_version / fingerprint`

---

## 九、实现阶段（按顺序）

### Phase 1（已完成）
- `InstanceTraceCache.h/.cpp`
- `VoiceBindingMap.h/.cpp`
- `DecisionEngine.h/.cpp`（audio-led 事件的 L1/L2/L3 框架）
- `PlayPathHook / EventNormalizer / FormSemanticCache`

### Phase 2（已完成）
- `Footstep` 轨道化、token renderer、cadence/liveness、truth probe、truth-first trigger
- `Footstep` 已从“音频主触发”切到“typed truth 主触发”

### Phase 3（下一步）
- `FootstepAudioMatcher`
- `Footstep` 改成：`TruthTrigger + ModifierResolver(L1/L2/L3) + LaneRenderer`
- 音频只做：
  - 强度
  - 左右偏置
  - release 长度
  - 材质/纹理修饰

### Phase 4（后续重构）
- 把 `StructuredEventPolicy` 推广到 `Hit / Block / Swing`
- 有稳定 truth 的事件：改为 `TruthTrigger + ModifierResolver`
- 没 truth 的事件：继续保留现有 `DecisionEngine(L1/L2/L3)` 主判定

---

## 十、重构指导（强制口径）

1. **不要再把音频重新拉回 Footstep 主触发**  
   `Footstep` 的主触发已经由 typed truth 接管，后续只能加强 modifier，不应恢复音频直触发
2. **有 truth 的事件，先 truth 后 modifier**  
   先保主触发，再用 L1/L2/L3 做精修
3. **没 truth 的事件，继续走现有 L1/L2/L3**  
   不要因为 `Footstep` 成功就把 `DecisionEngine` 误删
4. **Renderer-owned lane 不回退**  
   `Submit` 只交 token / intent，`Reader/HID` 继续拥有包络、节拍、停步、发送
5. **modifier 必须 fail-open**  
   匹配不到音频修饰时，保留默认 truth 模板，不得丢事件
6. **不允许运行时 EditorID 字符串匹配回流热路径**

---

## 十一、实现原则（必须坚持）

- 主链稳定优先于极限效果
- 所有增强模块可单独开关
- 新模块默认保守启用
- 运行时仅查表与短窗检索
- 真值事件优先于音频推断
- 任一模块异常不得阻塞触觉主输出（fail-open）
- **EditorID 只在构建期使用，运行期禁止字符串匹配**

---

## 十二、当前进度（2026-03-06）

### 已完成
- `InstanceTraceCache / VoiceBindingMap / DecisionEngine` 已稳定
- `SemanticRuleEngine + FormSemanticCache + fingerprint + 磁盘缓存` 已稳定
- `PlayPathHook` 已打通 `voice+generation -> instanceId -> source/sound formId`
- `EventNormalizer / SemanticResolver` 已接入运行时纯查表语义解析
- `DynamicPool` 已支持 shadow / learn-from-L2 / 灰度保护
- 发送层已从旧 slot/carry 迁移到 state-track scheduler + renderer-owned lane
- `Footstep` 已完成：
  - `AdmissionPolicy`
  - `token renderer`
  - `cadence/liveness`
  - `FootstepTruthProbe`
  - `shadow trigger`
  - `truth-first trigger`
  - `truth_audio_suppress`

### 当前现状
- `Footstep` 已不再由音频主触发，当前链路是：  
  `BGSFootstepEvent -> TruthTrigger -> lane=2 renderer -> HID`
- `Footstep` 的音频脚步主触发已被抑制，音频细节修饰尚未重新接回
- `L1/L2/L3` 当前职责已经分化：
  - 对 `Footstep`：未来转为 `ModifierResolver`
  - 对其它无 truth 事件：继续作为主判定引擎

### 已记录待办（问题收敛后执行，保留）
- 状态驱动输出扩充（defer）：在“丢震/乱震/静止误震”问题闭环后，再扩到 `Mixer` 统一事件包络与 `DecisionEngine` 状态门控，避免在根因未收敛时叠加变量。
- Deadline Tx Scheduler（P0+）：将当前“固定 lookahead + 固定每轮发送上限”的轮询发送，升级为“按 `qpcTarget` 截止时间驱动（EDF）”的发送器；目标是从机制上降低 `flush/no_select` 与 `lookMiss`，减少参数调节依赖。

### 第三方案可扩展点（已记录，保留）
1. `HidOutput`：先落地 EDF 截止时间驱动发送（主改造点）。
2. `HapticMixer`：扩展为“事件持续窗 + 尾部释放”的统一包络，给 EDF 提供更稳定的 `qpcTarget` 与 sustain/release 边界。
3. `DecisionEngine`：扩展为“事件状态门控”（按事件族节流/合并），减少短时间重复触发导致的无效调度压力。
4. `DynamicHapticPool`：扩展为“deadline-aware 回放候选”（仅在主链 miss 且不破坏截止时间时介入）。
5. `MetricsReporter/SessionSummary`：新增 deadline 口径指标（`deadlineHitRate`、`deadlineMissP95`、`noSelectFutureOnlyRatio`、`capHitBudgetPressure`）。

### 已确认临时隔离项（2026-03-06）
1. `Shout/Voice` 当前会在 `lane=1` 形成高频 live 输出，已确认会造成乱震。
2. 在未建立单独 `truth + renderer` 路径前，`Shout` 先做 live 硬隔离，避免污染 `Footstep/Combat` 体感。
3. 后续若恢复，必须走独立事件策略，不再复用当前 `swing/utility` 共享行为。

### Truth + ModifierResolver 扩充计划（新的重构指导）
1. `Footstep` 先补 `FootstepAudioMatcher shadow`，验证 truth 后短窗音频修饰匹配率
2. `Footstep` 再接 `L1Exact / L2Stat / L3Template` 修饰层，不改 truth 主触发
3. `Hit / Block / Swing` 若找到 typed truth，按同一模板迁移
4. 无 truth 事件继续保留现有 `DecisionEngine(L1/L2/L3)` 主链
5. 长期目标：统一为  
   `TruthTrigger(若有) -> ModifierResolver(L1/L2/L3) -> Renderer-owned Lane -> HID`

### 回滚与开关
- `enableFormSemanticCache`
- `enableL1FormSemantic`
- `semanticForceRebuild`
- `enableDynamicPoolLearnFromL2`
- `dynamicPoolMinConfidence` / `dynamicPoolL2MinConfidence`
- `dynamic_pool_resolve_min_hits` / `dynamic_pool_resolve_min_input_energy`
- `enable_state_track_scheduler`
- `enable_state_track_footstep_truth_trigger`
- `enable_state_track_footstep_context_gate`
- `enable_state_track_footstep_motion_gate`
- 任一异常保持 fail-open，不阻断 HID 主输出

---

## 十三、FAQ（防遗忘）

### Q1：为什么运行时禁止 EditorID 字符串匹配？它不是有助于提高命中吗？
- EditorID 对语义分类很有价值，但应放在**启动期/离线构建期**使用
- 热路径（Submit/Scorer/Mixer/ModifierResolver）做字符串匹配会带来额外 CPU 抖动、分支不稳定和线程风险
- 运行时需要可重复、低锁、可解释：因此统一用 `FormID/InstanceID/Voice+Generation` 查表
- 结论：**不是不用 EditorID，而是把它前移到预热阶段**

### Q2：启动期用 EditorID 全量预热后，运行时是怎么起作用的？
1. 启动扫描声音相关 Form，基于 `EditorID + 规则` 分类，生成 `FormID -> SemanticMeta`
2. 结果持久化到 `DualPadSemanticCache.bin`，并通过 fingerprint 校验一致性
3. 运行时通过 `PlayPathHook + InstanceTraceCache` 拿到 `source/sound formId`
4. `DecisionEngine / ModifierResolver` 用 `FormID` O(1) 查 `FormSemanticCache`
5. 全流程不依赖运行时字符串匹配，兼顾命中率与稳定性

### Q3：为什么 Footstep 改成 truth-first 后，L1/L2/L3 还有用？
- 对 `Footstep`：L1/L2/L3 不再负责“要不要触发”，而是负责“怎么修饰得更细”
- 对 `Hit/Block/Swing/Bow/Voice` 等无 truth 事件：L1/L2/L3 仍然是主判定主链
- 结论：**不是废掉 L1/L2/L3，而是重分配职责**

### Q4：拿到真值后，对应音频怎么找？
1. 先让 truth 保证这次事件成立
2. 再用 `qpcTruth` 为中心，在 `AudioFeatureRing` 里做短窗匹配
3. 用 `time + voice + meta + energy` 找“最可信修饰候选”
4. 找到就做强度/左右/时长/材质修饰；找不到就保留默认模板
5. 结论：**真值解决“有没有”，音频解决“像不像”**
```

## State Track Scheduler Plan (2026-03-06)

### Goal
- Replace slot/carry send path with state-driven track scheduler (Intent -> TrackState -> EDF output).
- Keep old slot/carry path as fallback switch during migration.

### Phase A (Done)
- Add config switches and parameters:
  - `enable_state_track_scheduler`
  - `state_track_lookahead_min_us`
  - `state_track_repeat_keep_max_overdue_us`
  - `state_track_release_hit_us`
  - `state_track_release_swing_us`
  - `state_track_release_footstep_us`
  - `state_track_release_utility_us`
- Add `TrackState` lanes in `HidOutput` and wire new path entry points:
  - `SubmitFrameNonBlocking` -> `SubmitFrameStateTrack`
  - `FlushPendingOnReaderThread` -> `FlushStateTrackOnReaderThread`
  - `GetReaderPollTimeoutMs` -> `GetReaderPollTimeoutMsStateTrack`
- Keep old path intact behind feature flag.

### Phase B (Next)
- Track envelope refinement:
  - attack/sustain/release per lane with stronger duration binding.
  - lane-local refractory and duplicate merge to reduce oscillation.
- EDF selection refinement:
  - tie-break by lane priority + freshness score.
  - explicit stale culling before repeat guard.
- Interim flow-through fix:
  - preselect cooldown gate for same-output state tracks, so repeat suppression happens before `track/select`.
  - wake timing follows earliest resend-ready instant instead of spinning inside repeat guard.
 - stop debounce on rendered lanes: repeated structured zero-stop submits collapse if `stopPending` is already queued or the lane is already inside the short stop release window.

### Phase C (Next)
- Extend state tracks to `Hit/Swing/Footstep/Block/Bow/Voice` separately.
- Add session summary metrics for new scheduler:
  - track select hit-rate
  - deadline miss p95
  - repeat-drop ratio
  - no-due future-only ratio

### Rollback
- Set `enable_state_track_scheduler=false` to restore legacy slot/carry scheduler.

### Phase B1 (Done - 2026-03-06)
- Replace single mutable `TrackState` with lane-owned `active + pending`.
- `SubmitFrameStateTrack` no longer overwrites the currently consumed active lane state.
- Cooldown ownership stays on the lane via `nextReadyUs`, not on per-submit `seq`.
- `FlushStateTrackOnReaderThread` promotes `pending -> active` only when the lane becomes eligible again.
- Goal: remove submit/send races that were still causing `track/repeat_guard`, `cooldownBlock=1`, and `over=3000us+` clusters.

### Phase B2 (Done - 2026-03-06)
- Start state-renderer migration on the `Footstep` lane only.
- Footstep lane now wakes and selects by lane cadence (`nextReadyUs`) after first activation, instead of chasing every submit deadline.
- Reader sends at most one Footstep render tick per flush, then waits for the next lane-ready instant.
- Rollback remains `enable_state_track_scheduler=false` or `enable_state_track_footstep_renderer=false`.

### Phase B3 (Done - 2026-03-06)
- Extend state-renderer coverage from `Footstep` to the `Impact` lane (`HitImpact + Block`).
- Fix the reader-side wake mechanism: `HidReader` no longer forces nonblocking mode while also relying on `hid_read_timeout()`.
- Goal: make scheduler timeout effective, reduce `track/no_due cooldownBlock=1`, and let non-footstep structured foreground events leave the old deadline-only path.
- Rollback:
  - `enable_state_track_impact_renderer=false`
  - or `enable_state_track_scheduler=false`

### Phase B4 (Done - 2026-03-06)
- Extend state-renderer coverage to the `Swing` lane (`WeaponSwing/SpellImpact/BowRelease/Shout`).
- Add a top-level cooling-only early return in `FlushStateTrackOnReaderThread()` so reader wakeups caused by HID input traffic do not repeatedly execute no-op lane scans.
- Reduce verified-ok probe noise:
  - `track/submit` now suppresses normal Footstep submits
  - `track/promote` now suppresses normal Footstep promotions
  - `track/select` now suppresses normal Footstep selects unless they are late/anomalous
- Rollback:
  - `enable_state_track_swing_renderer=false`
  - or `enable_state_track_scheduler=false`

### Phase B5 (Done - 2026-03-06)
- Tighten zero-frame handling in `SubmitFrameStateTrack()`:
  - drop `Unknown + 0/0 + no structured intent` events before they enter any lane
  - keep explicit zero-output stop semantics only for structured stop intent on the target lane
- Replace the old "any zero frame clears all tracks" behavior with lane-local structured stop clearing.
- Goal: remove `Unknown 0/0` submit noise, stop accidental global `stopFlush`, and preserve state-render release timing for real foreground lanes.
- Rollback:
  - revert `HidOutput::SubmitFrameStateTrack()` zero-frame gating logic
  - or `enable_state_track_scheduler=false`

### Phase B6 (Done - 2026-03-06)
- Rewrite `Tx` timing metrics to state-track semantics:
  - `renderP50/renderP95/renderN` = render deadline overdue distribution
  - `firstP50/firstP95/firstN` = event-to-first-render distribution
- Keep legacy queue latency sampling internal, but remove it from primary `Tx` / `TxStats` / `SessionSummary` logs while state-track scheduler is the main path.
- Reduce verified zero-frame diagnostic noise:
  - `ProbeTxDiag` no longer highlights `fgZero` ratio as a primary trigger
  - diagnostic output now focuses on `renderP95/firstP95/stale/bgWhileFg`
- Goal: make send-layer observability match the current state-driven scheduler instead of the retired queue model.

### Phase B7 (Done - 2026-03-06)
- Add lane-owned supersede semantics to state tracks:
  - each track slot now carries `epoch`
  - rendered lanes mark old `active` as `supersededAtUs` when a newer `pending` epoch arrives
  - flush promotes `pending -> active` with reason `supersede` once the lane becomes ready
- Reset stale `nextReadyUs` when a lane has no live `active/pending`, so idle lanes do not revive with multi-second-old deadlines.
- Goal: replace parameter-style stale trimming with lane-local event succession, and eliminate long-delayed replays caused by stale lane cadence.

### Phase B8 (Done - 2026-03-06)
- Start the option-2 responsibility split inside state tracks:
  - `Submit` only writes lane intent (`pending` / `stopPending`) and no longer shortens `active.releaseEndUs`
  - structured zero frames become lane-local `stopPending` intent instead of immediate `active+pending` clear
  - rendered lanes no longer drop submit updates via `submit_repeat`; repeated submits merge into lane intent
  - `Reader/HID` exclusively owns `promote / release / stop` transitions
- Goal: remove the remaining `submit_repeat`, `zero_stop`, `supersede_arm`, and `release_drop` cross-thread lifecycle conflicts without reverting the state-track scheduler.

### Phase C1 (Recorded - future option 3)
- Tokenized event renderer:
  - `Submit` writes lane-scoped event tokens or a tiny ring instead of mutating render state directly
  - `Reader/HID` consumes tokens on a fixed render cadence and produces the final `attack/sustain/release` envelope
  - duplicate suppression moves from motor-value equality to `event key + phase`
  - per-lane stop/supersede become token/state-machine operations instead of ad-hoc slot mutation
- Target:
  - further reduce dropped/chaotic haptics once option-2 lifecycle split is stable
  - provide the clean expansion path for `Hit/Swing/Block/Bow/Voice`
  - unify future `HapticMixer` envelope binding and session metrics around token/render semantics

### Phase B9 (Done - 2026-03-06)
- Promote `Footstep` from pending/supersede switching to the first full `lane token coalescer + renderer` path:
  - pre-first-render submits merge straight into `active` instead of creating supersede pressure
  - post-first-render submits coalesce into a lane-local token (`pending`) and refresh the renderer on lane cadence
  - `Footstep` no longer depends on `supersede -> promote` as its primary continuity mechanism
- New rollback switch:
  - `enable_state_track_footstep_token_renderer`
- Generalization rule:
  - `Footstep` is now the reference template for other structured lanes
  - future `Hit/Swing/Block/Bow/Voice` migrations should reuse the same model:
    - `Submit` only coalesces lane tokens
    - `Reader/HID` owns `attack/sustain/release`
    - event continuity is preserved by token refresh, not by slot overwrite or deadline chasing

### Phase B10 (Done - 2026-03-06)
- Upgrade `Footstep` from zero-stop-driven release to cadence-driven liveness:
  - structured `zero_stop` is no longer the primary stop truth for the Footstep token lane
  - `Submit` updates lane cadence (`cadenceLastTokenUs`, `cadenceExpectedGapUs`) and extends a silence-based `livenessDeadlineUs`
  - `Reader/HID` converts silence-gap expiry into stop/release, so stop is decided by missing future footstep tokens rather than by noisy zero frames
- Goal:
  - remove early stop decisions while movement is still active
  - reduce stop-boundary chaotic haptics without lengthening send latency
- General template for future structured lanes:
  - shared shell: `token coalescer + reader-owned renderer + lane-local end-policy`
  - cadence lanes (current `Footstep`): end by silence-gap/liveness timeout
  - burst lanes (future `Hit/Block/Swing`): reuse the same shell, but swap the end-policy to fixed burst/release or token-refresh-limited sustain instead of silence-gap

### Phase B11 (Done - 2026-03-06)
- Add `FootstepAdmissionPolicy` in front of the state-track token lane:
  - `ContextGate`: reject footstep token admission outside gameplay-like contexts
  - `MotionGate`: require current player motion or a short recent-motion window before a non-zero footstep token can enter the lane
  - keep the gate out of hot render/release logic; it only filters submit-time admission
- Goal:
  - stop load/menu/HUD transition false positives from entering the Footstep lane
  - reduce static-idle false pulses before they become renderer state

### Phase C2 (Recorded - future structured event policy)
- Promote the current `FootstepAdmissionPolicy` into a shared `StructuredEventPolicy` shell for `Hit/Block/Swing`:
  - `AdmissionGate`: context + owner/motion/source truth before token admission
  - `OwnerLatch`: keep a short burst/window ownership once a trusted source wins the lane
  - `LanePolicy`: cadence lanes use silence-gap end-policy; burst lanes use burst/release or refresh-limited sustain
  - `Reader-owned renderer`: `Submit` only coalesces tokens/intents; `Reader/HID` owns final `attack/sustain/release`
- Goal:
  - reduce per-event ad-hoc fixes
  - keep future migrations on one reusable track template instead of new lane-specific heuristics

### Phase B12 (Done - 2026-03-06)
- Add `FootstepTruthProbe` as a typed truth-event observer:
  - register a sink on `BGSFootstepManager -> BGSFootstepEvent`
  - only observe and log player-owned footstep truth samples
  - do not alter the current audio/state-track output path yet
- Output:
  - low-noise runtime samples: tag / context / moving / recent / admissible
  - session summary counters for total / player / non-player / context / motion admissibility
- Goal:
  - verify whether typed footstep truth is stable enough to replace audio-only footstep triggering

### Phase B13 (Done - 2026-03-06)
- Add `FootstepTruthProbe` shadow-trigger alignment without changing the live output path:
  - keep a short ring of admissible player footstep truth events
  - match `lane=2 / Footstep` first-render outputs against the recent truth ring
  - record `shadow match / truth miss / render miss / delta p50/p95 / pending truth` in runtime stats and session summary
- Goal:
  - quantify whether current footstep output cadence is aligned with typed footstep truth before promoting truth to the main trigger
- Thread model:
  - `BGSFootstepEvent` sink appends admissible truth into a small mutex-protected ring
  - `Reader/HID` reports footstep first-render samples into the same ring for one-shot shadow matching
  - no live haptics routing is changed yet; fail-open if the shadow probe misbehaves

### Phase B14 (Done - 2026-03-06)
- Promote `Footstep` to `truth-first trigger`:
  - admissible player `BGSFootstepEvent(FootLeft/FootRight)` now submits the primary `Footstep` trigger into `HidOutput`
  - current audio-derived `Footstep` submits are suppressed as primary triggers while truth-first mode is enabled
  - `FootTruth` shadow stats remain active to validate trigger-to-render delta after the switchover
- Truth-gate rule:
  - once `enable_state_track_footstep_truth_trigger=true`, typed player footstep truth is gated by gameplay context only
  - motion/recent-motion gating remains for audio-derived footstep admission, but must not veto typed player footstep truth
- Current scope:
  - audio is no longer the main trigger for `Footstep`
  - audio-derived footstep texture refinement is deferred; current switchover is intentionally strict to eliminate false/late footstep pulses first
- Rollback:
  - `enable_state_track_footstep_truth_trigger=false`
- Goal:
  - eliminate audio-driven false positives and late/duplicated footstep triggers
  - make `Footstep` the first full `truth-first, renderer-owned` structured event lane in the project

### Phase B15 (Done - 2026-03-06)
- Add `FootstepAudioMatcher` as a shadow-only modifier probe:
  - audio hot path writes a fixed-size recent feature ring without changing live output
  - admissible `Footstep` truth events enter a pending queue and are matched only after the forward lookahead window closes
  - matching uses current runtime assets only: `VoiceBindingMap + InstanceTraceCache + FormSemanticCache + AudioFeature`
- Current role:
  - quantify whether `Footstep` truth can reliably recover nearby audio for later strength/pan/duration refinement
  - keep `truth-first` output unchanged while measuring `match/noWin/noSem/lowScore`, `delta`, `score`, `duration`, and `panAbs`
- Next intended promotion:
  - if B15 shadow data is stable, upgrade `Footstep` from `TruthTrigger + default template` to `TruthTrigger + ModifierResolver(L1/L2/L3) + LaneRenderer`

### Phase B16 (Done - 2026-03-06)
- Add `FootstepTruthBridge` as the missing upstream causal bridge:
  - `FootstepTruthProbe` now publishes admissible player `FootLeft/FootRight` truth tokens into the bridge
  - `PlayPathHook(init/submit)` now publishes `Footstep`-typed `instanceId + voicePtr + generation` observations into the bridge
  - `AudioFeatureMsg` now captures `instanceId + voiceGeneration` at submit time, so later matching is not forced to re-derive everything from the current voice binding
- Bridge semantics:
  - this is `TruthEvent -> Instance/Voice -> Audio` causality, which complements the old runtime L1 (`Audio -> Instance -> Semantic`)
  - `FootstepAudioMatcher shadow` now prefers bridge-backed identity candidates first, and only falls back to short-window guessing when no causal candidate exists
- Current role:
  - validate whether `Footstep` can move from `TruthTrigger + default template` to `TruthTrigger + L1 exact modifier`
  - keep live `truth-first Footstep` output unchanged while measuring `FootBridge` claim quality and `FootAudio` bridge-backed match quality

### Phase B17 (Done - 2026-03-06)
- Replace the bridge-side `FootstepAudioMatcher` logic with a patch-bus shadow flow:
  - `TruthToken -> BridgeClaim -> PendingPatch -> first fresh exact instance/voice feature`
- Fallback policy:
  - bridge-bound truths first wait for a causally-bound audio patch
  - only after patch expiry do they fall back to the old short-window matcher
- Why this exists:
  - stop selecting multi-second stale audio features after a valid causal bridge already exists
  - make `Footstep` modifier resolution closer to the final `TruthTrigger + ModifierResolver + LaneRenderer` architecture
- Current role:
  - keep live `truth-first Footstep` output unchanged
  - preserve comparable shadow counters: `matched / noWin / noSem / lowScore / bridge(bound/match/noFeat)`

### Phase B18 (Done - 2026-03-06)
- Upgrade `Footstep` from `truth-first + shadow modifier` to `truth-first + live late-patch`:
  - keep first attack owned by `FootstepTruthProbe`
  - let causal audio patch only modify `lane=2` sustain/release/pan/amp after first render
- Live patch policy:
  - only bridge-backed matches can enter live patch bus
  - short-window matcher remains shadow/fallback and does not touch live output
  - patch target is exact `truthUs`, not nearest-window guessing
- Why this exists:
  - preserve `100% correct trigger` from truth-first path
  - add audio texture without reintroducing false trigger / late trigger / double trigger
  - make `ModifierResolver` converge to `causal patch bus` instead of retrospective ring scoring

### Phase B19 (Done - 2026-03-06)
- Add `Footstep patchable lease` and truth-anchored tail target:
  - `leaseExpireUs` keeps a rendered `Footstep` lane alive for late audio patching, but does not imply a long strong vibration by itself
  - `targetEndUs` is now anchored to `truthUs + matchDeltaUs + matchDurationUs`, not `patchArriveUs + hold`
- Renderer policy:
  - after `releaseEndUs`, `lane=2` can enter a dormant-but-patchable state until lease expiry
  - late patch may revive or extend only the tail portion of the same step
  - first attack remains truth-owned and must never be retriggered by audio
- Why this exists:
  - bridge-backed audio modifiers currently arrive tens of milliseconds later than the truth trigger
  - patchable lease absorbs that delay without handing trigger ownership back to audio
  - truth-anchored end time keeps total haptic duration aligned to the matched audio segment instead of drifting with patch arrival time

### Phase B20 (Done - 2026-03-06)
- Add `Footstep recent modifier memory` as the secondary fallback for audio-only testing:
  - cache only recent modifier parameters (`ampScale / pan / targetEndDelta`), never replay old motor output
  - when `enable_state_track_footstep_truth_attack=false` and a live patch misses, silent truth seeds may be lit by `recent_memory`
- Safety policy:
  - this is parameter memory, not event replay, so it does not substitute the current step with the previous step's full waveform
  - it exists to keep audio-only test mode closer to final handfeel without reintroducing old-step ghosting

### Phase B21 (Done - 2026-03-06)
- Move `recent modifier memory` from flush-time probing to truth-time priming:
  - when a player `Footstep truth` arrives in `truthAttack=false` mode, prime a `recent_memory` live patch immediately if recent modifier samples exist
  - later causal `audio_patch` may still override the same truth token
- Why this exists:
  - flush-time fallback was too late and produced large miss counters without actually rescuing whole silent steps
  - truth-time priming turns fallback into a per-step guarantee instead of a per-tick retry

### Phase B22 (Recorded - future silence-compensation generalization)
- Generalize `recent modifier memory` into a shared fallback pattern for truth-backed structured events:
  - reuse only recent modifier parameters such as `amp/pan/targetEndDelta/texture`
  - never replay previous motor output verbatim
  - only engage when `TruthTrigger` exists but live modifier patch misses or arrives too late
- Intended targets:
  - `Footstep` first
  - later `Hit/Block/Swing` once those event families move to `TruthTrigger + ModifierResolver + LaneRenderer`
- Design goal:
  - smooth unavoidable silence without reintroducing false triggers, phase drift, or previous-event ghosting

### Phase B23 (Done - 2026-03-06)
- Upgrade `Footstep recent modifier memory` from flat averaging to bucketed predictive memory:
  - separate `FootLeft / FootRight / JumpUp / JumpDown`
  - prefer exact-bucket samples; allow limited walk-family fallback only between left/right
  - use robust aggregation (`weighted median` for amp/targetEnd, trimmed weighted mean for pan) instead of plain averaging
  - fold in truth-gap similarity so memory prefers samples from a similar cadence
- Safety rule:
  - only real `audio_patch` updates the memory pool
  - `recent_memory` patch itself does not recursively write back, to avoid self-reinforcing drift

### Phase B24 (Done - 2026-03-06)
- Add `FootstepTruthSessionShadow` as the shadow prototype for `TruthSession + CandidateBus`:
  - create an active truth session when a player `Footstep` truth arrives
  - append Top-K `instance/voice/generation` candidates into that session instead of relying only on one-shot pairing
  - consume audio features incrementally from a lock-free ring and finalize sessions by event flow, not by per-truth full ring rescans
- Current role:
  - shadow-only, does not modify the stable live `truth-first + patch bus` output
  - measures whether session-driven finalization can improve claim coverage and reduce `no_window`
- New metrics:
  - `FootSession truths/inst/feat/patched/expired/noCand/noFeat`
  - `candP50/P95`
  - `truthToPatchP50/P95`
  - `claimToPatchP50/P95`

### Phase B25 (Done - 2026-03-06)
- Add `FootstepCandidateReservoir` as the current candidate-coverage upgrade for `TruthSession`:
  - collect recent footstep-like candidates from `PlayPath init`, `PlayPath submit`, and `EngineAudioTap` feature-side observations
  - seed each new truth session from the reservoir before waiting for later incremental candidates
  - keep the current live `truth-first` / `late-patch` path unchanged while evaluating whether `noCand` drops
- New metrics:
  - `FootCand obs/init/submit/tap`
  - `expired/snap/returned/active`

### Phase B26 (Done - 2026-03-06)
- Add `FootstepTagNormalizer` on the `truth -> shadow session` path:
  - normalize `FootLeft/FootSprintLeft` into the same stride family (`StrideLeft`)
  - normalize `FootRight/FootSprintRight` into the same stride family (`StrideRight`)
  - keep `JumpUp/JumpDown` separate
- Split `FootstepTruthSessionShadow` policy by gait:
  - `Sprint` sessions now use a wider candidate acceptance window and a longer session lifetime
  - `Walk/Sprint` dual tags inside a short window collapse into one stride session, with `Sprint` taking precedence
- Scope:
  - shadow/session side only
  - current live `truth-first` trigger remains unchanged until sprint coverage is revalidated

### Phase B27 (Done - 2026-03-06)
- Promote `FootstepTruthSessionShadow` into the primary live audio-modifier source for `lane=2`:
  - `TruthSession`-finalized features now enqueue `session_patch` live patches directly
  - `HidOutput` applies `session_patch` before consulting the older matcher-owned live patch queue
- Keep rollback and safety:
  - `FootstepAudioMatcher` still owns `recent_memory` and remains the fallback when `session_patch` is unavailable
  - `enable_footstep_truth_session_live_patch=false` restores the older matcher-owned live patch path
- Goal:
  - let the stronger `Walk/Sprint`-aware session model improve sprint coverage in live output without disturbing the stable `truth-first` trigger

### Phase B28 (Done - 2026-03-06)
- Move `Footstep` live modifier handling one step closer to `StrideState`:
  - `session_patch` is no longer treated as a one-shot consume-and-drop payload
  - each `truthUs` now owns a persistent session modifier state with a monotonic revision
  - `HidOutput lane=2` pulls newer revisions by `truthUs` and reapplies them from the footstep seed base instead of compounding patch scales
- Add claim-time provisional modifier:
  - when a stride session receives its first viable candidate, it can publish a `session_provisional` modifier immediately
  - later feature finalization upgrades the same truth-owned modifier state to a stronger `session_patch`
- Design intent:
  - reduce `q/ap/ex` loss caused by late queue consumption
  - make `truth_attack=false` test mode less dependent on a final audio feature arriving in time

### Phase B29 (Done - 2026-03-06)
- Pull `session_provisional` forward from `first candidate` to `truth arrival` for zero-attack test mode:
  - when `enable_state_track_footstep_truth_attack=false`, `FootstepTruthSessionShadow` now seeds a provisional live modifier at truth time instead of waiting for `claim`
  - candidate/feature arrival still upgrades the same truth-owned state later
- Make silent footstep seeds wake the reader as soon as a session live patch is already available:
  - `GetReaderPollTimeoutMsStateTrack()` now treats `lane=2` silent seeds with a ready session patch as immediately due
- Design intent:
  - cut `truth -> session_provisional` latency in zero-attack mode
  - improve sprint coverage without disturbing the normal `truth-first` product path

### Phase B30 (Done - 2026-03-06)
- Start collapsing `Footstep` from external patch handoff into `lane=2` stride state:
  - `TrackSlotState` now owns `footstepGait/footstepSide`
  - provisional/final modifier fields (`amp/pan/targetEnd`) now live with the stride slot
- `truth_attack=false` no longer depends on an external patch message to let a stride light up:
  - truth seeds a gait-aware provisional stride state immediately
  - `session/audio/recent_memory` now refine the same stride state instead of deciding whether the step exists
- `Walk/Sprint/Jump` provisional defaults are split by gait, so sprint no longer inherits walk assumptions by default
- Design intent:
  - remove remaining `sessionLive expired` handoff losses
  - make zero-attack test mode closer to the final truth-backed architecture without reintroducing old audio-only trigger problems

### Phase B31 (Done - 2026-03-06)
- Move same-side `Walk -> Sprint` upgrade into the truth/live submit boundary:
  - when `FootLeft + FootSprintLeft` or `FootRight + FootSprintRight` arrive inside a short coalesce window, `lane=2` now upgrades the existing stride state to `Sprint` immediately
  - no longer wait for a later `session_patch` to correct an already-seeded walk provisional
- Scope:
  - same-side stride family only
  - upgrade is limited to a short near-truth window, so unrelated later steps cannot hijack the current stride
- Design intent:
  - restore sprint coverage in zero-attack mode
  - keep gait selection as an upstream truth/live decision instead of a downstream patch repair

### Phase B32 (Done - 2026-03-06)
- Convert `FootSession` live modifier storage from an external consume-and-drop patch queue into a persistent truth-owned modifier state:
  - each `truthUs` now keeps the latest modifier revision alive until its own expiry window
  - `lane=2` reads the current modifier state by `truthUs + revision`, instead of depending on queue timing
- Update rules:
  - provisional and final session modifiers both upsert the same truth-owned state
  - expiring an unapplied revision still counts as live expiry, but later revisions for the same stride are no longer lost just because an earlier queue item was consumed or popped
- Design intent:
  - remove remaining handoff-driven `sessionLive expired` losses
  - move `Footstep` closer to full `StrideState` ownership, where modifier lifetime follows the stride itself

### Truth-backed Renderer Template (Recorded)
- `Truth-owned provisional renderer`
  - not Footstep-specific; same pattern can be promoted to other truth-backed events
  - future targets: `Hit/Block/Swing`
- `Predictive/recent modifier memory`
  - also reusable for other truth-backed events
  - only reuses modifier parameters
  - never replays old full motor output
  - intended to lower unavoidable silence probability, not replace the truth trigger

### Future Candidate Coverage Plan (Recorded)
- Preferred end-state after B25 is an upstream causal handshake:
  - `TruthToken -> PlayInstance` direct ownership binding, instead of probabilistic candidate collection
  - once a stable play-creation hook is found, candidate coverage should stop depending on windows/reservoirs and move to deterministic ownership transfer

### Phase C3 (Recorded - future typed truth probe family)
- Generalize the same typed-event probe pattern to other structured events:
  - `Footstep`: `BGSFootstepManager -> BGSFootstepEvent`
  - `Impact/Hit`: prefer typed impact/combat impact events over audio inference where available
  - `Swing`: prefer animation or behavior truth signals, with audio only as texture refinement
- Shared rule:
  - first find a typed event source or stable engine consumer
  - then validate ownership and cadence with a probe
  - only after truth quality is confirmed, promote it into the main trigger path

### Phase B33 (Done - 2026-03-06)
- Record the remaining `coverage / noCand / live-expired` cleanup as a later pass; current priority shifts to reducing `Footstep` haptic homogeneity without disturbing the truth-first trigger path.
- Add `TextureRenderer v1` to `lane=2`:
  - preserve the existing `TruthTrigger + StrideState` correctness path
  - replace the old single-pulse `amp/pan/end` feel with gait-owned texture envelopes
  - introduce `WalkSoft / SprintDrive / JumpLift / LandSlam` as the first stride texture families inside `HidOutput`
- Add rollback flag:
  - `enable_state_track_footstep_texture_renderer`

### Phase B34 (Done - 2026-03-06)
- Add `WeaponSwingTruthProbe` as the first combat truth-first entry point.
- Current scope is intentionally narrow:
  - player-only `BSAnimationGraphEvent`
  - only mapped `WeaponSwing` family tags submit live `EventType::WeaponSwing`
  - `Block/HitImpact` are deferred so attack truth can be verified in isolation
- Add low-noise probe logs:
  - `site=truth/submit`
  - `site=truth/reject reason=unmapped_attack_like`
- Add feature flag:
  - `enable_state_track_weapon_swing_truth_trigger`

### Phase B35 (Done - 2026-03-06)
- Add `HitImpactTruthProbe` on `TESHitEvent` as the first stable combat truth-backed path.
- Scope:
  - player-caused or player-targeted `TESHitEvent`
  - blocked hits promote to `EventType::Block`
  - non-blocked hits promote to `EventType::HitImpact`
  - both submit directly as `BaseEvent` into `lane=0`
- Goal:
  - get stable impact-lane combat vibration working before chasing a better `WeaponSwing` truth source
  - separate "combat truth never arrived" from "impact lane renderer failed"
- Add feature flag:
  - `enable_state_track_hit_truth_trigger`

### Phase B36 (Done - 2026-03-06)
- Start the `StructuredEventPolicy` shell extraction from the existing `Footstep` lane instead of growing more lane-local ad-hoc fields:
  - add `StructuredModifierState`
  - add `FootstepStrideState`
  - add `StructuredEventState`
- `lane=2` now stores `seed / gait / side / texture / modifier / lease` under `TrackSlotState::structured`, instead of scattering Footstep-only fields across the slot.
- Design intent:
  - make the current `Footstep` implementation match the documented `TruthTrigger + ModifierResolver + Renderer-owned Lane` architecture more closely
  - create the data-layout shell that `Hit / Block / Swing` can reuse later, instead of cloning another Footstep-specific field pack
- Scope:
  - this pass is structural only
  - live behavior should remain functionally equivalent
  - remaining `coverage / noCand / live-expired` cleanup stays deferred as already recorded above

### Phase B37 (Done - 2026-03-06)
- Extend the new `StructuredEventState` shell to combat burst lanes:
  - add `CombatBurstState`
  - add `StructuredRendererKind::CombatBurst`
  - let `lane=0 / lane=1` seed structured combat burst state instead of staying on raw motor pulses only
- Add `CombatTextureRenderer v1` for structured combat events:
  - `HitImpact -> HitImpact burst`
  - `Block -> BlockSnap`
  - `WeaponSwing -> SwingArc`
- Design intent:
  - stop cloning Footstep-only state patterns for combat lanes
  - make future `Hit / Block / Swing` truth-backed work land on the same renderer-owned shell from the start

### Phase B38 (Done - 2026-03-06)
- Normalize `WeaponSwing` truth tags into one trigger family instead of only accepting raw `weaponSwing` tags:
  - accept `weaponSwing / weaponLeftSwing / weaponRightSwing`
  - accept `AttackWinStart*`
  - accept `PowerAttack_Start*`
  - treat `AttackWinEnd / attackStop / *_end` as non-trigger attack phases
- Add a short truth-level dedupe window so paired attack-start tags do not double-submit the same empty swing.
- Strengthen `SwingArc` perceptibility in `CombatTextureRenderer`:
  - longer target end window
  - stronger provisional amplitude
  - clearer windup/cut/trail texture split
- Goal:
  - make empty swings land on a broader and more realistic attack-tag family
  - make `lane=1` WeaponSwing output easier to perceive without pulling audio back into the primary trigger path

### Phase B39 (Done - 2026-03-07)
- Upgrade modifier dimensionality from `amp/pan/end` only to audio-shaped texture parameters:
  - `StructuredModifierState` now carries `attackScale / bodyScale / tailScale / resonance / textureBlend`
  - `FootstepAudioMatcher` and `FootstepTruthSessionShadow` now derive and transport these values from matched audio features
- Promote renderer ownership of waveform/envelope detail:
  - `FootstepTextureRenderer` now uses audio-derived modifier shape to alter attack/body/tail texture, not just total strength and end time
  - `CombatBurst` tail-only audio refinement now writes the same modifier dimensions for future combat texture work
- Design intent:
  - stop treating audio as a trigger source while still borrowing the useful part of `audio-to-haptics`: waveform family, envelope shape, and truth-anchored end-time refinement
  - reduce haptic homogeneity without reintroducing false trigger / late trigger behavior

### Phase B40 (Recorded - 2026-03-07)
- Fine-grained haptics / audio-to-haptics refinement is temporarily frozen.
- Current branch is kept as the archive point for:
  - `TruthTrigger + ModifierResolver + Renderer-owned Lane`
  - `Footstep` truth-first path
  - early combat truth probes and texture renderer shells
  - audio-shaped modifier dimensions (`attack/body/tail/resonance/textureBlend`)
- Short-term product direction:
  - stop expanding fine-grained haptic behavior for now
  - use game-native vibration as the practical runtime choice until renderer / texture design knowledge is ready to resume
- Resume rule:
  - when development restarts, continue from this snapshot instead of re-opening old ad-hoc patch paths
  - keep the deferred items (`coverage`, `candidate coverage`, `session/live-expired`, richer texture design) as backlog, not active work
