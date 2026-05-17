# Windows Local Context

This file is for machine-specific context on this Windows machine and is intentionally separate from the shared `AGENTS.md`.

## Local Mod / Game Environment

- 当前 `xmake.lua` 里写死的 Skyrim 游戏根目录：
  - `G:/g/SkyrimSE`
- 当前 `xmake.lua` 里写死的 DualPad 插件部署目录：
  - `G:/skyrim_mod_develop/mods/dualPad/SKSE/Plugins`
- 当前外部 live `favoritesmenu.swf` 参考路径：
  - `G:/skyrim_mod_develop/mods/dualPad/Interface/favoritesmenu.swf`
- 当前日志路径：
  - `C:/Users/xuany/Documents/My Games/Skyrim Special Edition/SKSE/DualPad.log`

## Current Local Deployment Notes

- `xmake build DualPad` 的 `after_build` 当前会自动复制：
  - `DualPad.dll` / `.pdb`
  - `config/DualPadDebug.ini`
  - `config/DualPadMenuPolicy.ini`
  - `config/controlmap_profiles/DualPadNativeCombo/Interface/Controls/PC/controlmap.txt`
- `xmake build DualPadDInput8Proxy` 的 `after_build` 当前会自动复制：
  - `dinput8.dll` / `.pdb`
  - `tools/dinput8_proxy/DualPadDInput8.ini`
- 如果后续修改了本机部署路径或外部 live artifact 路径，只更新本文件，不要把这些绝对路径写进共享文档。

## Recommended Commands For This Repo

- `xmake build DualPad`
- `xmake build DualPadMenuContextPolicyTests`
- `xmake run DualPadMenuContextPolicyTests`
- `python3 scripts/dev/setup_graphify_local.py`
- `python3 scripts/dev/setup_graphify_local.py rebuild --reason manual-closeout`

## Local Reminders

- 当前 repo 内没有 `FavoritesMenu` 的 repo-owned SWF workspace；如果要对照 `G:` 盘上的 live artifact，只把它视为本机参考，不要把它写成共享 truth。
- 如果后续需要记录更多 Windows 本机内容，优先补这里，例如：
  - 本机工具链路径
  - 本机 MO2 / Skyrim 实例路径
  - 本机推荐验证命令
