# Keyboard Native 清理记录

日期：2026-03-11

## 目标

在不破坏当前已验证成功路线的前提下，持续移除旧实验入口、无效探针和宽日志，把 `keyboard-native` 收成可维护、可继续迭代的实现。

## 当前保留的成功路线

- `dinput8` proxy 作为 keyboard-native 的正式注入边界
- `Jump` 的 `sub_140C11600 +0x121` scoped coexistence patch
- second-family 的 gameplay-root validate 主线
- `Route B` 的手柄模拟量正式路径

## 已完成清理

### 1. 旧 InputLoop 探针链

已删除：

- `InputLoopEntryHook`
- `InputLoopProcessProbeHook`
- `MaybeInstallInputLoopProcessProbe(...)`
- `WriteInputLoopEntryProbeStub(...)`

### 2. `sub_140C11600` 内部实验 hook

已删除：

- `InstallPreprocessInternalCallExperiments(...)`
- `PreprocessIteratorBeginCallHook`
- `PreprocessSkipCheckCallHook`
- `PreprocessIteratorEndCallHook`
- `PreprocessLookupCompareCallHook`
- `PreprocessDescriptorLookupCopyCallHook`
- `PreprocessDescriptorWriteCallHook`
- `PreprocessFinalField18WriteCallHook`
- `PreprocessLateField18WriteCallHook`
- `PreprocessConditionCallHook`

### 3. 旧 Poll / coexistence 宽日志

已删除或停用：

- `CoexistenceValidate producer`
- `CoexistenceValidate inject`
- `CoexistenceValidate proxyState`
- `CoexistenceValidate proxyData`
- 旧 `Poll` descriptor-assign / control-event 实验安装

### 4. 旧 external worker / runtime probe 残留

已删除：

- old DirectInput / external worker 实验 hook 块
- 历史 raw probe stub 和 call-site 实验残留

### 5. 孤立的无引用辅助函数与常量

已删除：

- `LogKeyboardFamilyProbe(...)`
- `DescribePreprocessHelperTarget(...)`
- `LogPreprocessIteratorState(...)`
- `LogGlobalCompareExpectedSlotWriteIfNeeded(...)`
- 对应的 preprocess helper RVA 常量

## 当前明确保留，不参与清理的内容

以下模块仍然属于现役主线，暂不动：

- `dinput8` bridge 及其运行时消费逻辑
- `Jump` 的 preprocess gate patch
- second-family 的 gameplay-root validate 路线
- `Route B`
- 仍在支撑当前主线的少量摘要函数与安装入口

## 当前状态

代码已经从“历史实验和现役主线混杂”的状态，收到了“成功路线为主，少量必要诊断保留”的状态。

后续清理只继续针对：

- 已确认没有安装入口的旧 helper
- 只剩定义、不再参与成功路线的旧日志和格式化函数

不会再动当前已验证成功的路线本身。
# 2026-03-11 补充：GetDeviceData 旧快照诊断精简

已完成：

- 删除 `DiObjDataHook` 中旧的 manager/local/hidden snapshot 采集与对比
- 删除对应的 envelope 宽日志：
  - `GETDEVICEDATA HIDDEN ...`
  - `GETDEVICEDATA WRAPPER/INJECTION LOCAL ...`
  - `GETDEVICEDATA WRAPPER/INJECTION HIDDEN ...`
  - manager jump snapshot 级联输出
- 删除不再使用的辅助结构与函数：
  - `ManagerJumpSnapshot`
  - `KeyboardPollLocalSnapshot`
  - `KeyboardHiddenStateSnapshot`
  - `CaptureManagerJumpSnapshot(...)`
  - `CaptureKeyboardPollLocalSnapshot(...)`
  - `CaptureKeyboardHiddenStateSnapshot(...)`
  - `LogManagerJumpSnapshot(...)`
  - `LogKeyboardHiddenStateSummary(...)`
  - `LogKeyboardHiddenStateDiff(...)`
  - `LogKeyboardPollLocalSnapshotDelta(...)`
  - `LogPreprocessSpaceChainProfile(...)`

当前 `DiObjDataHook` 只保留：

- bridge 主线事件注入
- `GETDEVICEDATA WRAPPER BUFFER`
- `GETDEVICEDATA RETURN BUFFER`
- 最小摘要 `GetDeviceData envelope ...`
# 2026-03-11 补充：收尾清理完成

本轮继续把 `KeyboardNativeBackend.cpp` 中已经完全脱离当前成功路线的旧脚手架摘掉，并确认构建、部署、回归都保持正常。

本次清理：

- 删除 `AcceptDump` 整条旧调试链残留：
  - `kAcceptDumpWrapperSize`
  - `kAcceptDumpEventSize`
  - `kAcceptDumpArg3Size`
  - `MarkPendingAcceptDump(...)` 的调用
  - `Reset()` 里对应的旧 `g_acceptDump*` 状态清零
- 删除 second-family 旧 validate/compare 脚手架：
  - `SecondFamilyValidateSpec`
  - `kSecondFamilyValidateSpecs`
  - `ResolveSecondFamilyValidateSpec(...)`
  - `MatchesSecondFamilyValidateFallback(...)`
  - `ResolveSecondKeyboardFamilyAction(...)`
  - `ResolveKeyboardFamilyActionByScancode(...)`
  - `LogSecondKeyboardFamilyCompareSnapshot(...)`
  - `SecondFamilyTraceContext`
  - `LogCompareExpectedSlotWriteIfNeeded(...)`
  - 对应的旧 RVA 常量 `kActivateValidateTargetRva / kSneakValidateTargetRva / kSprintValidateTargetRva`

当前保留的成功路线：

- `dinput8` bridge
- `Jump` 的 `sub_140C11600 +0x121` coexistence patch
- `Route B`
- 当前最小化的 dispatch/preprocess 摘要与安装入口

结果：

- `xmake build DualPad` 通过
- `DualPad.dll` 已重新部署到 `G:\skyrim_mod_develop\mods\dualPad\SKSE\Plugins\DualPad.dll`
- `Jump` 与手柄模拟量回归已通过
