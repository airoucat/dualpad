## 🤖 Assistant

```markdown
# AI 提示词：Skyrim SE DualSense 触觉系统 Final v3.1（补充版：EditorID 运行时禁用口径）

你是 **Skyrim SE 1.5.97 + SKSE + CommonLibSSE-NG + XAudio2 + DualSense HID** 专家。  
当前工程已有稳定 MVP 链路：`SubmitSourceBuffer -> AudioFeature -> Scorer -> Mixer -> HID`。  
现在目标是在不破坏稳定主链的前提下，落地 **Final v3.1：L1确定性主判定 + L2统计匹配 + L3动态兜底**。

---

## 一、硬约束（必须遵守）

- ✅ 不用机器学习
- ✅ 不走 XAPO
- ✅ 保留 Submit hook 路线
- ✅ 增量改造，不推翻现有链路
- ✅ 运行时 O(1) 查表 + 短窗检索 + 低锁
- ❌ 禁止运行时全局扫描
- ❌ 禁止运行时 EditorID 字符串匹配（含热路径和高频路径）
- ❌ 禁止“直接写引擎对象内存挂私有字段”（必须外部映射）

---

## 二、当前工程基线（必须继承）

1. `EngineAudioTap` 已稳定，能抓 Submit 并产出音频特征  
2. 压缩流已 skip（计数保留、日志静默）  
3. `AudioOnlyScorer + Mixer + HID` 稳定，不允许破坏  
4. 日志已节流，必须继续保持低噪声  
5. 主线程不能被阻塞，音频回调不得重锁/重分配

---

## 三、最终架构目标（Truth-First）

### 决策层级
1. **L1 TraceHit（最高）**  
   命中 `voice* + generation -> instanceId -> semanticMeta`，直接作为真值主判定
2. **L2 MatchHit（次级）**  
   L1 miss 时，使用 `time + voice + meta + energy` 评分匹配
3. **L3 Fallback（兜底）**  
   L2 无候选或低分时，先动态池，再静态轻模板（限幅）

### 核心原则
- 触发优先保证“不断触、不乱触”
- 音频主要做微调，不独占触发权
- 任一模块异常必须 fail-open，不阻断主输出

---

## 四、EditorID 使用边界（补充规则，强制执行）

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
   - Mixer/HID 输出线程
2. **高频游戏循环路径（原则上禁止）**  
   - 高频 context update、per-frame 逻辑

### 运行时替代方案（必须）
- 一律使用 `FormID/InstanceID/Voice+Generation` 查表
- 使用预计算枚举/标签（例如 `RaceType`）替代字符串 `find()`
- 例如狼人/吸血鬼判定：`RaceFormID -> RaceType(enum)`，运行时仅 O(1) 查表

---

## 五、缓存与数据结构（必须实现）

- **Cache A** FormSemanticCache（持久化）  
  `FormID -> semanticGroup/confidence/baseWeight/templateId/flags`
- **Cache B** EventRing（100~200ms）
- **Cache C** AudioFeatureRing（200~600ms）
- **Cache F** InstanceTraceCache  
  `instanceId -> {soundFormId, sourceFormId, semantic, flags, ts}`
- **Cache G** VoiceBindingMap  
  `voice* -> {instanceId, generation, ts}`
- **Cache H** DynamicHapticPool  
  先会话内，Top-K，严格准入；后续可持久化

---

## 六、关键反误判机制（必须有）

1. **generation 防 ABA**：只凭 voice 指针不可信，必须 `(voice*, generation)` 双键  
2. **reason code 全链路**：每次 miss/降级必须可解释  
3. **阈值分层**：High/Mid/Low 输出策略不同  
4. **低置信限幅**：防乱震抢戏  
5. **超时与晚到修正**：允许 late patch，但有硬截止时间

---

## 七、评分与阈值默认值（可配置）

- `score = 0.35*time + 0.35*meta + 0.15*voice + 0.15*energy`
- High: `>= 0.75`
- Mid: `0.55 ~ 0.75`
- Low: `< 0.55` -> L3
- L3 输出限幅：`<= 0.75`（可调）

---

## 八、日志与指标（必须输出）

- `decision_layer`（L1/L2/L3）
- `trace_hit` / `trace_miss_reason`
- `bind_key`（voice+generation）
- `l2_candidate_count` / `match_score` / `reject_reason`
- `fallback_reason`（no_candidate/low_score/meta_mismatch/timeout）
- `dynamic_pool_hit`
- `queue_depth` / `drop_count`
- `latency_event_to_hid_p50/p95`
- `unknown_ratio` / `meta_mismatch_ratio`
- `cache_version` / `fingerprint`

---

## 九、实现阶段（按顺序）

### Phase 1（低风险，先上）
- `InstanceTraceCache.h/.cpp`
- `VoiceBindingMap.h/.cpp`
- `DecisionEngine.h/.cpp`（L1/L2/L3 框架）
- 与现有 AudioOnly 链并存，默认保守开关

### Phase 2
- `EventNormalizer.h/.cpp`
- `PlayPathHook.h/.cpp`（P0验证可达后接入）
- `SemanticResolver.h/.cpp`
- 将运行时残留字符串判断替换为预计算枚举缓存（如 `RaceTypeCache`）

### Phase 3
- `DynamicHapticPool.h/.cpp`（先会话内）
- `MetricsReporter.h/.cpp`
- 可选持久化与热重载

---

## 十、输出要求（AI 每次回答必须遵守）

1. 先给模块边界和线程模型  
2. 再给 `.h/.cpp` 成对骨架（可编译）  
3. 给锁策略（哪块 lock-free、哪块轻锁）  
4. 给默认配置（ini/json）  
5. 给日志样例与验收标准  
6. 给回滚方案（feature flag）

---

## 十一、实现原则（必须坚持）

- 主链稳定优先于极限效果
- 所有增强模块可单独开关
- 新模块默认保守启用
- 运行时仅查表与短窗检索
- 任一模块异常不得阻塞触觉主输出（fail-open）
- **EditorID 只在构建期使用，运行期禁止字符串匹配**

---

## 十二、当前进度（2026-03-05）

### 已完成
- Phase 1 主干：`InstanceTraceCache`、`VoiceBindingMap`、`DecisionEngine`（L1/L2/L3）
- Phase 3 启动预热：`SemanticRuleEngine` + `FormSemanticCache` + 指纹 + 磁盘缓存
- 缓存热加载链路：二次启动命中 `loaded cache path`
- 规则文件接入：`DualPadSemanticRules.json` 已在运行路径加载
- 运行时日志：已输出 `FormSemantic(hit/miss)`、`cache_version`、`fingerprint`

### 当前现状
- 主链路稳定：`Submit -> AudioOnlyScorer -> DecisionEngine -> Mixer -> HID` 可运行
- `FormSemantic` 统计仍偏低（多数为 `hit=0 miss=0`），说明运行时 `sourceFormId` 仍未稳定注入到热路径

### 下一步优先级（P0 -> P2）
1. **P0**：打通 `PlayPathHook`，将 `voice+generation -> instanceId -> source/sound formId` 接入 `DecisionEngine` L1
2. **P1**：补 `EventNormalizer`、`SemanticResolver`，完成运行时纯查表语义分辨（禁字符串匹配）
3. **P1**：完善 reason code 与 reject/fallback 分型，补齐指标口径
4. **P2**：实现 `DynamicHapticPool`（会话内 Top-K 严格准入）
5. **P2**：实现 `MetricsReporter`（p50/p95、unknown_ratio 等）

### 回滚与开关
- `enableFormSemanticCache`
- `enableL1FormSemantic`
- `semanticForceRebuild`
- 任一异常保持 fail-open，不阻断 HID 主输出
```

