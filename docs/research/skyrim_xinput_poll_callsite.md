# Skyrim XInput Poll call-site 调查

本文记录 `S-DP5-RC20-HOTFIX` 对 Skyrim SE 1.5.97 XInput Poll 路径的静态与动态证据。结论按「已证明」「推导」「未验证」分层；IDA 函数名、单次日志或主菜单采样都不能单独证明固定线程或每帧调用次数。

## 二进制身份

| 项目 | 值 |
| --- | --- |
| 支持版本 | Skyrim SE 1.5.97.0 |
| IDA 输入 | unpacked `SkyrimSE.exe`，34,586,624 bytes |
| IDA 输入 SHA-256 | `DE92095A18513FCAFFE5A86FD72879D3350C61CDCDB8500B7D31DF2BAE9579CD` |
| 实机 packed EXE | 34,769,792 bytes |
| 实机 packed EXE SHA-256 | `5666E1BDDD01BCAB31ECF11691EF1A3F22E1541AF79F2BC0E55318533CFE5D12` |
| IDA image base | `0x140000000` |
| Address Library | `version-1-5-97-0.bin` |

unpacked IDA 输入与 packed 实机 EXE 的 hash 不同是预期事实；RVA 和版本用于关联二者，不能把两个 hash 写成同一个 artifact。

## Poll 与 call-site

| 项目 | RVA | VA | 证据 |
| --- | ---: | ---: | --- |
| `BSWin32GamepadDevice::Poll` | `0xC1AB40` | `0x140C1AB40` | RTTI `BSWin32GamepadDevice`、vtable `0x14175E920` 的 `+0x10` entry 指向该函数 |
| XInput call-site | `0xC1AB9D` | `0x140C1AB9D` | 指令 bytes `E8 2C 17 00 00` |
| original target | `0xC1C2CE` | `0x140C1C2CE` | 单指令 thunk，跳转到 `__imp_XInputGetState` |
| `BSInputDeviceManager::PollInputDevices` | `0xC150B0` | `0x140C150B0` | Address Library ID `67315` |

`BSWin32GamepadDevice::Poll` 大小为 `0x29C`。函数在 call-site 前把 current state 复制到 previous fields，随后以 `ecx = [this + 0xC8]` 传入 XInput user index，以 `rdx = this + 0x100` 传入 caller-owned `XINPUT_STATE`。成功返回后，函数继续同步处理 14 个 button，并归一化 trigger/stick。

Windows x64 ABI 下，hook 必须保持：

- `RCX`：`dwUserIndex`；
- `RDX`：调用者提供、仅在本次同步调用期间有效的 `XINPUT_STATE*`；
- `EAX`：原 `XInputGetState` result；
- original call 的同步写入与返回语义。

DualPad thunk 每次只 acquire 一份 `shared_ptr<const PollOutputFrame>`，在完成本次同步序列化前保持该 immutable frame 存活；不得缓存 caller 的 `RDX` 指针。

## Caller / callee 图摘要

```text
BSInputDeviceManager::PollInputDevices (0x140C150B0)
  -> 遍历 this + 0x60 的 4 个 device slots
  -> virtual slot +0x10
     -> BSWin32GamepadDevice::Poll (0x140C1AB40)
        -> call-site 0x140C1AB9D
           -> thunk 0x140C1C2CE
              -> __imp_XInputGetState
```

`PollInputDevices` 之后继续执行 control-map、rumble、input event emit 和 queue reset。IDA 找到 3 个直接调用上下文：

- `0x1405B33B0`：位于大型 update 函数中，受非暂停条件控制；
- `0x1405B3F43`：位于一个短 helper 中；
- `0x14063FC52`：位于另一条循环路径中。

因此，同一个 gamepad Poll virtual entry 可以由多个逻辑 update context 到达。call-site 本身不是「主线程」或「每 rendered frame 恰好一次」的证明。

## Qualifying consumer

本调查中的 qualifying consumer 指：真正执行 `BSWin32GamepadDevice::Poll`，并以有效 user index 和 caller-owned state buffer 到达 `0xC1AB9D` 的一次调用。它不是：

- 任意 `BSInputDeviceManager` 调用；
- 任意 XInput import；
- 任意渲染帧；
- 任意拥有相同函数名的 helper。

如果多个 update context 最终调用同一 gamepad device virtual Poll，它们会共用同一个 `0xC1AB9D` call-site。当前静态图无法区分哪些 caller 在 gameplay、loading、paused 或后台路径中实际成为 qualifying consumer。

## 动态证据

### 匹配 build 主菜单与读档样本

build `1b3ca5a2bc7a` 的实机日志记录：

- runtime `1-5-97-0`；
- relocated Poll `0x7FF748D2AB40`；
- relocated call-site `0x7FF748D2AB9D`；
- relocated original target `0x7FF748D2C2CE`；
- hook operational state 为 `installed`；
- `nativeFavorites=false`；
- Main Menu target refresh 每次 `refreshed=1`，没有全 stack refresh。

同一会话还证明 `BSInputDeviceManager` event sink 的逻辑 owner 会跨 OS 线程串行迁移：线程 `29128` 完成 frame token `1-359` 后，Loading/Fader 建立期间由线程 `22420` 到达 token `360`。旧 guard 将此误判为永久 `thread_drift`。修复后的 owner 合同以 RAII ticket、`tickActive` 和严格单调 token 保证单 writer；仅在前一 ticket 已释放时允许显式 `event=rebound`，并发异线程仍 fail-closed。

该证据证明 input-pump owner 不能绑定为进程全生命周期固定 OS thread；它不等价于 Poll call-site 的完整线程分布。

### Capped Poll 诊断样本

一次较早、开启 `log_poll_diagnostics` 的主菜单采样记录了固定上限 256 组 enter/exit：单一观察线程、`inFlight` 最大值 1，调用间隔 P50/P95/P99 约为 16/17/18 ms。该 raw log 后续被新的 SKSE 启动覆盖，且当时日志尚未内嵌 build commit，因此只保留为辅助观察，不作为 matching-build 发布证据。

较旧的崩溃前日志曾观察到多个 Poll thread ID。它与单一主菜单样本不矛盾：二者来自不同生命周期和 build；当前证据只允许结论「不得假设固定 Poll thread」。

## 已证明、推导与未验证

### 已证明

- 1.5.97 的 `0xC1AB40 / 0xC1AB9D` 身份、指令 bytes、original target 和 calling convention。
- `PollInputDevices` 有多个静态 caller context。
- 实机 call-site patch 在匹配 runtime 上安装到预期 relocated 地址。
- input-pump owner 在读档生命周期中发生过串行 OS thread migration。
- Poll hook 的代码合同是 immutable output acquire + synchronous serialize，不再推进 runtime 或 pulse。

### 高概率推导

- 多个 update context 可能在不同 lifecycle 触发同一 gamepad Poll call-site。
- 主菜单约 60 Hz 的样本与 rendered/update cadence 一致，但不能据此宣称所有状态均为每帧一次。

### 未验证

- gameplay、loading、paused、后台状态下每个 rendered frame 的 qualifying consumer 数量。
- 所有实际 Poll caller 的 OS thread 分布与 owner/Poll 完整 ordering。
- synthetic DPadUp 到 Favorites user event、menu construct、movie load 和 event dispatch 的动态调用链。
- 与当前 DLL/PDB/log/config/SWF 完全匹配的 Favorites crash dump、exception code、faulting instruction 和符号化 stack。

## 当前发布影响

当前部署 build 为 `1edb1949ecc4`，DLL SHA-256 为 `F377AF16E628BF6CE30F67BF79EC4F3F4BCD65B99AF4D4E15992E9CFA4A66D81`，PDB SHA-256 为 `F6097998AC2C7EF866F8108F3CFE1CD8FE5C3FB70DD2C621D13FF79969B6248B`。该 build 已包含串行 owner handoff 修复，但仍需新的读档实机样本确认 `event=rebound` 后 generation 持续推进。

`enable_native_favorites=false` 继续是默认值。没有 matching dump 和真实 Favorites 循环前，IDA 结果不能解除该 gate；发布状态保持 `NO-GO`。
