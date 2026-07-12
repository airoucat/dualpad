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
| I-0 | NO-GO | engine query identity、device availability patch 均禁用 | 入口签名与 delegate slot 7 已静态确认；仍缺 handler vtable、runtime original target 与 availability 动态唯一出口 |
| I-P | NO-GO | current-cycle event mutation 禁用，仅保留 shadow audit | 缺 Poll receipt、event materialization 与下游 consumer 的动态顺序证明 |
| I-1 | NO-GO | 26 个 caller override 为零 | 26 个 direct xref 静态 inventory 已冻结；caller 尚未逐一命名并动态分类 |
| I-2 | NO-GO | shared transform scope/router 禁用 | 缺 ABI、caller、cache 与 Native/force A/B 实机结果 |
| I-MENU | NO-GO | menu direct callsite patch 禁用 | 缺四菜单每 epoch 单次 SetPlatform 的动态闭环 |
| I-CURSOR | NO-GO | 两个方向的坐标 side effect 均禁用 | 缺实例、坐标换算、read-back 与误差矩阵 |
| I-SPRINT | NO-GO | SprintHandler guard 不存在 | 缺 held-state/release watchpoint 的结果 A 或 B |
| I-KBM | NO-GO | raw reconcile 与 synthetic suppression 分别禁用 | 缺 complete physical-only provider；缺 S-A/S-B/S-C 独立裁决 |
| I-5 | NO-GO | 所有可选 patch group 保持禁用 | 6 个基线 signature site 已冻结；仍缺最终启用 site 全集、matching runtime transaction 与 partial rollback 证据 |

## 已自动证明但不解除动态门禁的内容

- neutral/unchanged HID 不推进 meaningful activity 或 owner。
- current-cycle 与 next-Poll 分别执行单 writer 检查。
- Sprint contributor mask、final release、epoch/session、Poll receipt、adapter rollback、cursor exact ack 与 engine Original-only 合同有自动化反例。
- static/host identity fixture 只能证明 fail-closed 逻辑；不能把任一 Gate 从 `NO-GO` 提升为通过。

## IDA 静态 inventory

matching IDA 9.3 数据库已绑定以下事实：

- 输入文件 SHA-256 为 `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD`，image base 为 `0x140000000`。
- `0x140C15240` 读取 `this + 0x70` 的 delegate device，并调用 `[vtable + 0x38]`，即 delegate slot 7；runtime 实际 target 仍未由 I-0 动态样本确认。
- `0x140C15240` 有且仅有 26 个 direct code xref；另有 1 个 data xref。26 个 code xref 当前全部保留为 `Unknown`，不得由地址邻近关系猜测 caller domain。
- `0x140C15240`、`0x140705AE0`、`0x140ECD970`、`0x140ED2F90`、`0x140C150B0` 与 `0x140C1AB40` 的入口字节已冻结为 6 个基线 signature site。

版本化 artifact 位于 `.dualpad-builder/mixed_input_ida_static_evidence.json`，重导出脚本为 `scripts/dev/export_mixed_input_ida_static.py`，校验命令为：

```powershell
python scripts/ci/check_mixed_input_ida_static_evidence.py
```

该 checker 只证明 matching binary/IDB 的静态 inventory 未漂移。它明确要求 `releaseRelevantUnknownCallers=26`、`dynamicGatesRemain=NO-GO`，不能替代 I-0、I-1 或 I-5 的动态出口。

## 多 Gate shadow 证据采集

运行时已提供有界、默认关闭的 mixed-input shadow 记录器。它向同一个 JSONL 写入两类记录：

- `runtime-shadow`：直接携带 callback 已消费的 `PollMaterializationReceipt`，以及同一轮 `Prepare -> Apply -> Commit` 的 plan、adapter audit、current-cycle-sensitive ledger 前后值和完整 Sprint contributor decision；它不会重新 Acquire 最新 Poll frame。
- `runtime-presentation-shadow`：携带 cursor plan 前后值、exact ack、requested/committed owner、menu identity、presentation epoch 和 Original-only engine snapshot。I-CURSOR 两个方向未裁决前，方向切换只允许保持 pending，不得凭硬编码 `NotRequired` 直接提交。

两类记录分别按 decision key 采样：首次、decision 变化、时钟回退或每 10 秒健康采样时才写入。输出失败不会抛出异常，并保留同一 decision 的后续重试资格。记录器使用同步文件 I/O，只能在短时动态证据采集时显式开启；日常运行必须保持关闭。

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

判定规则：命令输出 `"status": "PASS"` 且 `"violations": []`，只证明这份运行样本没有违反自动化 causal contract；输出 `FAIL` 则本轮证据明确失败。

该文件可以同时辅助：

- I-P：receipt、writer、adapter rollback 和 Sprint-sensitive ledger 对照；仍缺 event materialization 与下游 consumer watchpoint。
- I-CURSOR：两个方向的 plan/ack/menu identity 对照；仍缺真实坐标、read-back 和误差矩阵。
- I-SPRINT：G/K/M source mask、winning source、joining/non-final suppression、virtual bridge 和 final release 对照；仍缺 `heldStateActive` / `triggerReleaseEvent` hardware watchpoint。
- I-MENU/I-1：只提供 menu identity 与 Original-only owner snapshot，不记录真实 SetPlatform invoke 或 26 caller query，因此不能据此闭合 Gate。

所以 shadow trace 即使为 `PASS` 也不解除 I-P、I-CURSOR、I-SPRINT 或其它动态 Gate。采集完成后应把 `enable_trace_recording` 恢复为 `false`。

动态证据必须绑定被测插件 commit、Skyrim runtime、EXE/DLL/PDB/config hash、原始日志或 debugger artifact，并把唯一出口写回本台账和 `.dualpad-builder/mixed_input_evidence.json`。证据缺失、冲突或无法唯一判断时维持 `NO-GO`。
