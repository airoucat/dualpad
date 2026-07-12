# Skyrim 混合输入动态证据台账

## 适用范围

本台账只记录已批准正式实现计划 I 节的动态门禁，不以静态测试、host fixture 或 IDA 静态阅读替代 Skyrim 实机证据。目标仅限 Skyrim SE 1.5.97 与 CommonLibSSE-NG。

- 目标文件：`SkyrimSE.exe.unpacked.exe`
- SHA-256：`DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD`
- 当前 mixed-input 发布状态：`NO-GO`
- implementation base：`3985eea35a84ec7952a88f8dfd13c068391fc28b`

## 当前门禁裁决

| Gate | 当前结果 | Production capability | 未闭合证据 |
|---|---|---|---|
| I-0 | NO-GO | engine query identity、device availability patch 均禁用 | 缺 REL/vtable/slot/original target 的 matching runtime 唯一出口 |
| I-P | NO-GO | current-cycle event mutation 禁用，仅保留 shadow audit | 缺 Poll receipt、event materialization 与下游 consumer 的动态顺序证明 |
| I-1 | NO-GO | 26 个 caller override 为零 | release-relevant caller 尚未逐一动态分类 |
| I-2 | NO-GO | shared transform scope/router 禁用 | 缺 ABI、caller、cache 与 Native/force A/B 实机结果 |
| I-MENU | NO-GO | menu direct callsite patch 禁用 | 缺四菜单每 epoch 单次 SetPlatform 的动态闭环 |
| I-CURSOR | NO-GO | 两个方向的坐标 side effect 均禁用 | 缺实例、坐标换算、read-back 与误差矩阵 |
| I-SPRINT | NO-GO | SprintHandler guard 不存在 | 缺 held-state/release watchpoint 的结果 A 或 B |
| I-KBM | NO-GO | raw reconcile 与 synthetic suppression 分别禁用 | 缺 complete physical-only provider；缺 S-A/S-B/S-C 独立裁决 |
| I-5 | NO-GO | 所有可选 patch group 保持禁用 | 缺 matching runtime transaction、signature 与 partial rollback 证据 |

## 已自动证明但不解除动态门禁的内容

- neutral/unchanged HID 不推进 meaningful activity 或 owner。
- current-cycle 与 next-Poll 分别执行单 writer 检查。
- Sprint contributor mask、final release、epoch/session、Poll receipt、adapter rollback、cursor exact ack 与 engine Original-only 合同有自动化反例。
- static/host identity fixture 只能证明 fail-closed 逻辑；不能把任一 Gate 从 `NO-GO` 提升为通过。

## I-P shadow 证据采集

运行时已提供有界、默认关闭的 I-P shadow 记录器。它直接携带 callback 已消费的 `PollMaterializationReceipt`，以及同一轮 `Prepare -> Apply -> Commit` 的 plan、adapter audit 和 current-cycle-sensitive ledger 前后值；它不会重新 Acquire 最新 Poll frame。记录器仅在 decision 变化、时钟回退或每 10 秒健康采样时写入，输出失败会静默保持 fail-closed，不影响 owner tick。

在实机使用的 `DualPadDebug.ini` 中设置：

```ini
[Replay]
enable_trace_recording = true
trace_output_dir = Data/SKSE/Plugins/DualPadTrace
trace_session = mixed-input-i-p
```

退出游戏后，从 Skyrim 数据目录取得：

```text
Data/SKSE/Plugins/DualPadTrace/mixed-input-i-p/mixed_input_evidence.jsonl
```

在仓库根目录执行：

```powershell
python scripts/ci/evaluate_mixed_input_trace.py "G:\SteamLibrary\steamapps\common\Skyrim Special Edition\Data\SKSE\Plugins\DualPadTrace\mixed-input-i-p\mixed_input_evidence.jsonl"
```

判定规则：命令输出 `"status": "PASS"` 且 `"violations": []`，只证明这份运行样本没有违反自动化 causal contract；输出 `FAIL` 则本轮证据明确失败。该 shadow trace 不包含下游 consumer watchpoint，因此即使为 `PASS` 也不解除 I-P，仍需补齐 event materialization 与 consumer 顺序的动态证明。采集完成后应把 `enable_trace_recording` 恢复为 `false`。

动态证据必须绑定被测插件 commit、Skyrim runtime、EXE/DLL/PDB/config hash、原始日志或 debugger artifact，并把唯一出口写回本台账和 `.dualpad-builder/mixed_input_evidence.json`。证据缺失、冲突或无法唯一判断时维持 `NO-GO`。
