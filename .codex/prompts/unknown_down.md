你是 Skyrim SE 1.5.97 + SKSE + CommonLibSSE-NG 触觉系统重构工程师。

任务目标：
将当前触觉系统中的 Unknown 事件比例降到最低（优先级最高），并显著降低“乱震”。
允许大改代码，但必须保持线程安全、可观测、可调参、可编译。

【现有链路】
Submit -> EngineAudioTap -> VoiceManager -> AudioOnlyScorer -> DecisionEngine -> HapticMixer -> HidOutput

【重构目标链路】
Submit/PlayPath -> TraceHydrator -> SemanticFusion -> EligibilityGate -> DecisionRanker -> BudgetMixer -> HID

====================
一、必须实现的模块改造
====================

1) 新增 TraceHydrator（可并入 PlayPathHook 但建议独立）
文件：
- src/haptics/TraceHydrator.h
- src/haptics/TraceHydrator.cpp
功能：
- 聚合 voice binding + submit pContext + init object + instance trace
- 输出 HydratedContext:
  - sourceFormId
  - soundFormId
  - preferredEvent
  - semanticGroup
  - semanticConfidence
  - hasTraceMeta

2) 新增 SemanticFusionEngine
文件：
- src/haptics/SemanticFusionEngine.h
- src/haptics/SemanticFusionEngine.cpp
功能：
- 融合多个信号得到最终语义与置信度：
  - FormSemanticCache
  - TraceMeta
  - RuleEngine(Keyword/Exact/Override)
  - FormTypeHint
- 输出：
  - fusedGroup
  - fusedEventType
  - fusedConfidence
  - fusionReason(bitmask)

3) 新增 EligibilityGate
文件：
- src/haptics/EligibilityGate.h
- src/haptics/EligibilityGate.cpp
功能：
- 硬门控：先判定能不能震，再进入 Decision
- RejectReason 至少：
  - UnknownBlocked
  - BackgroundBlocked
  - NoTraceContext
  - LowSemanticConfidence
  - LowRelativeEnergy
  - RefractoryBlocked
  - BudgetExceeded

4) 重构 DecisionEngine（先 Gate 再 Rank）
文件：
- src/haptics/DecisionEngine.h
- src/haptics/DecisionEngine.cpp
要求：
- L1 命中必须：
  hasBinding && hasTraceMeta && (eventType!=Unknown || sourceFormId!=0)
- Unknown 默认 hard reject
- 可选 Unknown promotion（严格阈值）：
  如果 tracePreferredEvent 有效且 confidence >= cfg.unknownPromotionMinConfidence
  才允许从 Unknown 升级
- 扩展 Stats：
  gateAccepted/gateRejected
  rejectUnknownBlocked/rejectBackgroundBlocked/rejectNoTraceContext/rejectLowSemanticConfidence/rejectLowRelativeEnergy/rejectRefractoryBlocked
  unknownBuckets(no_formid/cache_miss/low_conf/no_trace/ambiguous/promoted)

5) 重构 HapticMixer（预算混音）
文件：
- src/haptics/HapticMixer.h
- src/haptics/HapticMixer.cpp
要求：
- 每 tick 仅混 TopN foreground（默认2）
- background 预算默认 0
- 同组策略支持 max（默认）与 avg（可配）
- 统计预算拒绝数 budgetDropCount

6) 收紧 AudioOnlyScorer 源头
文件：
- src/haptics/AudioOnlyScorer.h
- src/haptics/AudioOnlyScorer.cpp
要求：
- ampFloor 降到 0~0.02 默认
- 支持 relative energy gate（基于每voice noise floor EMA）
- confidence 不强制固定下限 0.35
- 新增统计 relativeEnergyDropped

====================
二、Unknown 闭环（必须）
====================

1) 新增 UnknownAnalytics（可内嵌到 MetricsReporter）
文件（建议）：
- src/haptics/UnknownAnalytics.h
- src/haptics/UnknownAnalytics.cpp
功能：
- Unknown 分桶计数（互斥）
- TopN 聚合（按 formId/editorID hash）
- 周期输出 Top20
- 支持导出候选规则（json）：
  Data/SKSE/Plugins/DualPadUnknownCandidates.json

2) 周期日志必须新增字段
- UnknownBucket(noForm/cacheMiss/lowConf/noTrace/ambiguous/promoted)
- UnknownTopN(...)
- Gate(accepted/rejected)
- Reject breakdown
- Budget drop

====================
三、配置扩展（必须）
====================

修改：
- src/haptics/HapticsConfig.h
- src/haptics/HapticsConfig.cpp

新增 ini 配置：
[Gate]
allow_unknown_audio_event=false
allow_background_event=false
enable_unknown_promotion=true
unknown_promotion_min_confidence=0.82
l1_form_semantic_min_confidence=0.75
relative_energy_ratio_threshold=2.8
refractory_hit_ms=70
refractory_swing_ms=45
refractory_footstep_ms=40

[Budget]
mixer_foreground_top_n=2
mixer_background_budget=0.0
mixer_same_group_mode=max

[UnknownAnalytics]
enable_unknown_topn=true
unknown_topn_size=20
unknown_export_candidates=true
unknown_export_interval_sec=60

默认值必须保守（宁可漏震，不可乱震）。

====================
四、工程约束
====================

- 不改变 EngineAudioTap 安装路径，不改成 XAPO。
- 不引入 ML。
- 运行时不得字符串全局匹配。
- 使用 O(1) 查询 + 原子统计 + 低锁设计。
- 不改动与触觉无关模块。
- 保持可编译；修复所有新增编译错误。
- 不删除现有关键日志，仅扩展。

====================
五、输出要求
====================

完成后输出：
1) 变更文件列表
2) 每个文件改动目的
3) 新增配置项及默认值
4) 示例日志（含 UnknownBucket/Gate/Reject/Budget）
5) 验收步骤：
   - Unknown ratio 对比
   - 误触主观验证
   - HID fail 验证