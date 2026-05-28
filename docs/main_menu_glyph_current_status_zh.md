# 主菜单动态图标当前实现状态

本文只描述当前已经落地并正在运行的主菜单动态图标兼容路径。长期 SVG / Widget 方案见 [dynamic_glyph_svg_system_plan_zh.md](dynamic_glyph_svg_system_plan_zh.md)，不替代本页的 current truth。

## 结论

当前主菜单 glyph 兼容路径是：

```text
Interface/startmenu.swf
  -> DualPad_GetActionGlyphToken / DualPad_GetActionGlyph
  -> ScaleformGlyphBridge shim
  -> ScaleformPromptAdapter
  -> PromptRuntimeOwner
  -> PromptService
  -> CompiledActionGraph + published presentation state
```

`ScaleformGlyphBridge`、`GlyphResolutionCompat` 和旧 SWF API 现在只承担 shim / adapter / compatibility boundary；它们不得持有 binding authority、trigger reverse lookup authority 或 menu fallback authority。

## 当前运行时链路

对应代码：

- [`src/input/glyph/ScaleformGlyphBridge.cpp`](../src/input/glyph/ScaleformGlyphBridge.cpp)
- [`src/input/glyph/GlyphResolutionCompat.cpp`](../src/input/glyph/GlyphResolutionCompat.cpp)
- [`src/input_v2/prompt/ScaleformPromptAdapter.cpp`](../src/input_v2/prompt/ScaleformPromptAdapter.cpp)
- [`src/input_v2/prompt/PromptRuntimeOwner.cpp`](../src/input_v2/prompt/PromptRuntimeOwner.cpp)
- [`src/input_v2/prompt/PromptService.cpp`](../src/input_v2/prompt/PromptService.cpp)

当前流程是：

1. 主菜单 SWF 通过 GameDelegate 调用 `DualPad_GetActionGlyphToken(actionId, contextName)` 或 `DualPad_GetActionGlyph(actionId, contextName)`。
2. `ScaleformGlyphBridge` 只转发注册、菜单打开和 replay compat 查询到 `ScaleformPromptAdapter`。
3. `ScaleformPromptAdapter` 将旧 SWF 查询转为 `PromptRuntimeOwner` 查询。
4. `PromptRuntimeOwner` 基于当前 active manifest、`CompiledActionGraph` 和 published presentation state 构造 `PromptService`。
5. `PromptService` 返回 `PromptDescriptor` / `PromptSnapshotRecord` 语义；旧 SWF API 再被投影成兼容 token / descriptor。

外部兼容合同保持：

- `DualPad_GetActionGlyphToken` 返回单个 token string。
- `DualPad_GetActionGlyph` 保持旧 SWF 兼容 descriptor shape；当前运行时可附带更多诊断字段，但旧页面不得依赖新字段作为必需输入。
- 旧 SWF 返回 shape 不在 PH8b 中修改。

## 已淘汰的旧 authority

以下说法只属于历史实现，不再是当前 reality：

- `ScaleformGlyphBridge` 直接向 `BindingManager` 查询 action -> trigger。
- glyph 解析依赖 trigger reverse lookup。
- menu fallback / context retry 是当前 glyph authority。
- 围绕 `BindingManager + token/descriptor` 继续扩展新的 glyph 合同。

如需考古这些路径，只能把它们当作历史背景，不能在 reviewed current-status 文档或 CI closeout 中恢复为当前指导。

## 当前已经支持的图标类型

当前兼容输出仍落到单个 ButtonArt token，因此稳定表达面保持为标准单键：

- 面键：`Square / Cross / Circle / Triangle`
- 肩键：`L1 / R1`
- 扳机：`L2 / R2`
- 系统键：`Create / Options`
- 方向键：`DpadUp / DpadDown / DpadLeft / DpadRight`

这些 token 的事实来源不再是 `BindingManager` 反查，而是 `PromptService` 对 `CompiledActionGraph` 与当前 prompt scope 的解析结果。

## 当前仓库边界

当前仓库里真正已经落地、可直接维护的动态图标资产包括：

- [`src/input/glyph/ScaleformGlyphBridge.cpp`](../src/input/glyph/ScaleformGlyphBridge.cpp)
- [`src/input/glyph/GlyphResolutionCompat.cpp`](../src/input/glyph/GlyphResolutionCompat.cpp)
- [`src/input_v2/prompt/ScaleformPromptAdapter.cpp`](../src/input_v2/prompt/ScaleformPromptAdapter.cpp)
- [`src/input_v2/prompt/PromptRuntimeOwner.cpp`](../src/input_v2/prompt/PromptRuntimeOwner.cpp)
- [`config/DualPadBindings.ini`](../config/DualPadBindings.ini)
- [`Interface/startmenu.swf`](../Interface/startmenu.swf)

当前不在仓库里的内容包括：

- `Interface/favoritesmenu.swf`
- `FavoritesMenu.as`
- `MappedButton.as`
- `ButtonPanel.as`
- 之前那套 `FavoritesMenu` 专项 SWF patch 工作区

如果后续任务重新落到 `FavoritesMenu`，第一步是恢复 SWF workspace 和页面源码，再重新做 artifact inventory。

## 当前还不支持的内容

当前主菜单兼容路径仍不是完整 Widget / SVG glyph system。它不能优雅表达：

- 背键
- `fn` 键
- 任意异形自定义键
- 组合键，例如 `A+B`
- layer / combo 的运行时图标拼装

原因是旧 SWF 兼容 API 仍以单 token 为主要输出。支持这些能力需要新的 Widget / image 表达和对应验证计划，不能通过恢复 `BindingManager` reverse lookup 或改旧 SWF 返回 shape 来解围。

## 对后续开发的直接含义

如果后面只维护主菜单单键图标替换：

- 只能沿 `ScaleformGlyphBridge -> ScaleformPromptAdapter -> PromptRuntimeOwner -> PromptService` 兼容路径做修正。
- 不能把 `BindingManager`、trigger reverse lookup、menu fallback 写回当前 authority。

如果后面要支持背键、`fn`、组合键、异形键或 Widget / SVG：

- 先新增治理/验证计划。
- 保持旧 SWF API 兼容层稳定。
- 不恢复 `FavoritesMenu` workspace 以外的页面级假设。
