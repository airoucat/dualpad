# DP5-RC20 U3 Release Notes

本文记录 DP5-RC20 U3 的 product integration and release readiness 结论。它不是新的 runtime phase，也不改变 `src/input_v2/` mainline。

## 支持范围

- 唯一 supported Skyrim runtime：Skyrim SE 1.5.97。
- 唯一正式 C++ 支持面：CommonLibSSE-NG。
- unsupported runtime 行为：fail-closed。`UpstreamGamepadHook` 不安装 Poll 内部 `XInputGetState` hook，`ControlMapOverlay` 不应用 native overlay，并输出明确日志原因。

## 安装检查

### clean install

- `SKSE/Plugins/DualPad.dll` 来自当前 release build。
- `DualPadDebug.ini`、`DualPadBindings.ini`、`DualPadMenuPolicy.ini` 与 `DualPadControlMap.txt` 要么来自本 release bundle，要么明确使用内建 defaults。
- 可选 game-root `dinput8.dll` / `DualPadDInput8.ini` 只在需要 keyboard-helper bridge output 时安装。
- `Interface/startmenu.swf` 仍是 repo-owned start menu compat artifact；`FavoritesMenu` workspace 不在本仓库内恢复。

### overwrite install

- `DualPad.dll` / `.pdb` 与可选 `dinput8.dll` / `.pdb` 可以被同一 source commit 的构建产物覆盖。
- xmake deploy 不覆盖已存在的用户 INI：`DualPadDebug.ini`、`DualPadMenuPolicy.ini` 与 `DualPadDInput8.ini` 仅在目标不存在时复制。
- 覆盖后必须重新生成 release artifact manifest，对比 source commit、generated docs hash 与 config manifest hash。

### rollback install

- 首选 runtime rollback：在 `DualPadDebug.ini` 设置 `use_upstream_gamepad_hook=false`。
- 二进制 rollback：恢复上一版 `DualPad.dll`，并按需恢复可选 `dinput8.dll`。
- 配置 rollback：恢复上一版 INI 或显式提供 `DualPad.Manifest.lkg.json`；不得把 stale / unsupported LKG schema 当作可用配置提升。

## Fail-Closed 行为

- missing config：两份主配置同时缺失时只允许编译 built-in defaults；不会从半缺失文件合成未验证 dirty action。
- bad config：runtime reload 失败时保留当前 active bundle，不提升新 epoch。
- stale config：启动时 bad config 加 stale / unsupported LKG schema 必须失败，不提升 active epoch。
- unsupported runtime：hook 与 native overlay 都不半安装，不进入 silent retry loop。
- hook install failure：relocation signature mismatch 或 trampoline patch 失败时保留 disabled route，并记录可诊断原因。

## 设备生命周期

- no-device：HID reader 保持重试 open，不输出 dirty input。
- start-connected：首次 open 后提交 reset，重置 live input facts，并绑定 haptics handle。
- hot-plug：后续 open 走同一 reset / fact reset / handle bind 路径。
- disconnect：read error 提交 reset，清空 live input facts，清空 haptics handle，关闭设备后重试。
- reconnect：重新 open 后从新的 software sequence 和 fresh facts 开始。

## Release Artifact Manifest

生成命令：

```powershell
python scripts/dev/generate_release_artifact_manifest.py --require-build-artifacts --expect-clean
```

输出：

- `build/release/DP5-RC20-release-artifact-manifest.json`
- `build/release/DP5-RC20-release-artifact-manifest.md`

manifest 记录 source commit、tracked dirty 状态、build outputs、config files、repo-owned SWF、generated docs、reviewed release docs、install checks、device lifecycle checks 与 non-goals。

## non-goals

- 不新增 runtime phase。
- 不改变 `input_v2` mainline。
- 不恢复旧 SWF shape、旧 `FavoritesMenu` workspace、`BindingManager` 或 trigger reverse lookup authority。
- 不在 DP5-RC20 U3 内生产最终图标视觉资产。
- 不把 DualSense haptics / vibration promotion 纳入本 milestone。
