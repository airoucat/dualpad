# 主菜单动态图标当前实现状态

本文只描述 **当前已经落地并正在运行** 的主菜单动态图标方案，不展开长期 SVG / Widget 规划。

## 结论

当前主菜单走的是：

- `ScaleformGlyphBridge`
- `DualPad_GetActionGlyphToken`
- `DualPad_GetActionGlyph`
- **单个 ButtonArt token 兼容链**

也就是说，主菜单现在还不是：

- `DualPadGlyphWidget`
- 运行时 SVG 渲染
- 组合键 `parts[]` 拼装

## 当前运行时链路

对应代码：

- [ScaleformGlyphBridge.cpp](../src/input/glyph/ScaleformGlyphBridge.cpp)

当前流程是：

1. 主菜单 SWF 通过 GameDelegate 调用 `DualPad_GetActionGlyphToken(actionId, contextName)`，或者调用 `DualPad_GetActionGlyph(actionId, contextName)`
2. `ScaleformGlyphBridge::ResolveActionToken(...)` 先向 `BindingManager` 查询当前 action 在对应 `InputContext` 下的 trigger
3. `ScaleformGlyphBridge::TriggerToButtonArtToken(...)` 把 trigger 映射成 **一个** ButtonArt token
4. `DualPad_GetActionGlyph` 当前只是在 token 外面包一层最小 descriptor：
   - `ok`
   - `buttonArtToken`
   - `semanticId`
   - `contextName`
5. 主菜单 SWF 目前仍然按原生 `ButtonArt / MappedButton` 思路显示图标

因此，当前主菜单图标替换的本质是：

- **动态 action -> trigger 解析**
- **但最终仍落到原生单 token 图标显示**

## 当前已经支持的图标类型

基于 [ScaleformGlyphBridge.cpp](../src/input/glyph/ScaleformGlyphBridge.cpp) 现状，当前兼容链已经可以稳定表达：

- 面键：`Square / Cross / Circle / Triangle`
- 肩键：`L1 / R1`
- 扳机：`L2 / R2`
- 系统键：`Create / Options`
- 方向键：`DpadUp / DpadDown / DpadLeft / DpadRight`

另外，`TriggerType::Axis` 里也已经对：

- `LeftTrigger`
- `RightTrigger`

做了 `LT / RT` token 映射。

## 当前仓库边界

当前仓库里真正已经落地、可直接继续开发的动态图标资产只有：

- [`src/input/glyph/ScaleformGlyphBridge.cpp`](../src/input/glyph/ScaleformGlyphBridge.cpp)
- [`config/DualPadBindings.ini`](../config/DualPadBindings.ini)
- [`Interface/startmenu.swf`](../Interface/startmenu.swf)

当前**不在仓库**里的内容包括：

- `Interface/favoritesmenu.swf`
- `FavoritesMenu.as`
- `MappedButton.as`
- `ButtonPanel.as`
- 之前那套 `FavoritesMenu` 专项 SWF patch 工作区

另外，当前 [`config/DualPadBindings.ini`](../config/DualPadBindings.ini) 里的 `[FavoritesMenu]` 额外按键只是：

- 临时动态图标验证映射

它的作用是确认页面底栏提示是否跟着映射层走，不代表仓库里已经存在：

- `Favorites.GroupConfirm`
- 页面级 glyph broker
- 页面级 execution broker

## 当前还不支持的内容

当前主菜单这条链 **还不能优雅支持**：

- 背键（back paddles）
- `fn` 键
- 任意异形自定义键
- 组合键，例如 `A+B`
- layer/combo 的运行时图标拼装

原因不是映射层完全不支持这些语义，而是：

- `ScaleformGlyphBridge` 目前只返回 **一个字符串 token**
- `TriggerToButtonArtToken(...)` 只处理 `Button / Tap / Hold / Axis`
- 不处理 `Combo / Layer`

所以当前主菜单方案的能力边界是：

- **标准单键可动态替换**
- **非标准形状或组合键暂时不走这条链**

## 与 SVG / Widget 方案的关系

- [dynamic_glyph_svg_system_plan_zh.md](dynamic_glyph_svg_system_plan_zh.md) 描述的是 **长期方案**
- 不是当前主菜单已经落地的实现

当前应这样理解：

- **主菜单当前正式方案**：`ButtonArt token` 兼容链
- **SVG / Widget 方案**：未来要统一动态图标系统时的正式长期目标

## 对后续开发的直接含义

如果后面只想继续扩展主菜单的单键图标替换：

- 可以继续沿用当前 `ScaleformGlyphBridge + token` 路线

如果后面要支持：

- 背键
- `fn` 键
- 组合键
- 非圆形 / 异形自定义键

那就不该继续硬扩单 token 路线，而应转向：

- `GlyphDescriptor`
- Widget / image 渲染
- `ButtonArt` 只保留为兼容层

如果后面重新回到 `FavoritesMenu` 页面级改造，第一步也不应该直接修页面逻辑，而应先恢复：

- 对应 SWF 页面源码
- 对应 patch 工作区
- 当前页面 artifact inventory
