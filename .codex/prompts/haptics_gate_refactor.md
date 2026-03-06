你是 Skyrim SE 1.5.97 + SKSE + CommonLibSSE-NG 触觉系统重构工程师。

目标：解决“乱震”问题。把现有链路从“尽量放行”改为“硬门控优先”：
Audio -> Candidate -> Gate -> Rank -> Budget -> Output

【硬要求】
1) 不改 Submit Hook 路线（EngineAudioTap/VoiceManager 保持）。
2) 运行时 O(1) 查表，不做字符串匹配。
3) Unknown/UI/Music/Ambient 默认不触发震动（除非显式放行）。
4) 先 gate 再 rank，再混音预算。
5) 必须有可观测拒绝原因统计。
6) 代码可编译，改动要完整落地，不留 TODO。
7) 保持现有日志风格（[Haptics][...]）。

====================
一、要做的架构改造
====================

A) 新增模块：HapticEligibilityEngine
- 新文件：
  - src/haptics/HapticEligibilityEngine.h
  - src/haptics/HapticEligibilityEngine.cpp
- 职责：对每个 HapticSourceMsg 做硬门控，返回 GateResult
- GateResult 至少包含：
  - bool accepted
  - RejectReason reason
  - HapticSourceMsg adjustedSource
- RejectReason 至少包含：
  - UnknownBlocked
  - BackgroundBlocked
  - NoTraceContext
  - LowSemanticConfidence
  - LowRelativeEnergy
  - RefractoryBlocked
  - BudgetExceeded (预算阶段用)
- 依赖：
  - HapticsConfig
  - EventNormalizer
  - FormSemanticCache / SemanticResolver
  - DecisionEngine（可只通过输入上下文，不互相环依赖）

B) DecisionEngine 重构为“先 gate 后 rank”
- 文件：
  - src/haptics/DecisionEngine.cpp
  - src/haptics/DecisionEngine.h
- 修改点：
  1. 在 L1/L2/L3 判定前先调用 EligibilityEngine。
  2. L1TraceHit 条件收紧：
     - 必须 binding 命中 AND trace/meta 上下文有效
     - 且 (eventType != Unknown 或 sourceFormId != 0)
  3. Unknown 默认 hard reject（由 gate 给出 reason）
  4. 保留现有 L2/L3 逻辑，但只对 gate 通过的候选执行。
- Stats 增加并导出：
  - gateAccepted
  - gateRejected
  - rejectUnknownBlocked
  - rejectBackgroundBlocked
  - rejectNoTraceContext
  - rejectLowSemanticConfidence
  - rejectLowRelativeEnergy
  - rejectRefractoryBlocked

C) HapticMixer 改预算混音（Budgeted Mixer）
- 文件：
  - src/haptics/HapticMixer.cpp
  - src/haptics/HapticMixer.h
- 修改点：
  1. 每 tick 仅取 TopN 前景源（默认 2，可配置）。
  2. 背景组默认预算为 0（或极低，受配置控制）。
  3. 同组叠加策略：优先 max/priority，不使用简单平均导致持续抖动。
  4. AddSource 前经过 gate（若 gate 在 Decision 层完成，则此处只做 budget reject）。
- Stats 增加：
  - budgetDropCount
  - activeForeground
  - activeBackground

D) AudioOnlyScorer 收紧源头
- 文件：
  - src/haptics/AudioOnlyScorer.cpp
  - src/haptics/AudioOnlyScorer.h
- 修改点：
  1. ampFloor 默认降到 0~0.02（不要固定 0.08）。
  2. minEnergy 支持动态噪声地板（relative energy）：
     - 维护每 voice 的简易 noiseFloor EMA
     - 若 (rms < noiseFloor * ratioThreshold) 则 drop
  3. confidence 不要强制最低 0.35，改成真实映射 + clamp。
- Stats 增加：
  - relativeEnergyDropped
  - unknownSourceProduced（若仍有）

E) 配置扩展（DualPadHaptics.ini）
- 文件：
  - src/haptics/HapticsConfig.h
  - src/haptics/HapticsConfig.cpp
- 新增配置项并实现解析：
  [Gate]
  - allow_unknown_audio_event=false
  - allow_background_event=false
  - allow_unknown_footstep=false
  - unknown_semantic_min_confidence=0.70
  - relative_energy_ratio_threshold=2.5
  - refractory_hit_ms=70
  - refractory_swing_ms=40
  - refractory_footstep_ms=45

  [Budget]
  - mixer_foreground_top_n=2
  - mixer_background_budget=0.0
  - mixer_same_group_mode=max   ; max|avg

- 确保默认值保守（宁可漏，不可乱）。

F) 观测日志升级
- 周期日志里增加：
  - Gate(accepted/rejected)
  - Reject(unknown/bg/noTrace/lowSem/lowRel/refractory/budget)
  - Budget(activeFg/activeBg/drop)
- 保持现有日志字段不破坏（向后兼容）。

====================
二、实现约束
====================
1) 不引入第三方大库。
2) 不改动与触觉无关模块。
3) 不删除现有统计，只扩展。
4) 多线程安全：统计用 atomic，读写路径低锁。
5) 编译失败必须自行修复到通过。
6) 修改后给出“文件变更清单 + 关键设计说明 + 默认参数”。

====================
三、验收标准（必须满足）
====================
1) 菜单/环境下明显不再持续乱震。
2) 日志可见 reject 分类增长，而不是直接产生 source。
3) HID fail 维持 0，A/B/C 心跳正常（若保留）。
4) Decision 中 L1 不再因为仅 voice binding 而泛命中。
5) Unknown 事件在默认配置下不进入最终输出。
6) 提供建议调参区间（不需要长文，给表格或列表）。

请直接修改代码并输出：
- 变更文件列表
- 每个文件改动目的
- 新增配置项默认值
- 一段示例统计日志格式
