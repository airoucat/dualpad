# Deferred: 当前页 SWF Presentation 刷新问题

当前已确认一条独立于 ownership 主线的问题：

- 菜单 `PresentationOwner` 已经切换
- `menu->RefreshPlatform()` 也已经被调用
- 但某些当前打开的页面不会立刻重刷自己的 DualPad 动态图标
- 用户必须先切到别的页面再切回来，图标/平台表现才会更新

目前观察到的情况：

- 主菜单相关页面里，只有部分页面根脚本实现了 `DualPad_OnPresentationChanged()`
- C++ 侧已经尝试在 `RefreshPlatform()` 后调用 `_root.DualPad_OnPresentationChanged()`
- 但暂停菜单等其它页面仍未立即更新，说明这些 SWF 页面需要各自补专门的 presentation-changed 刷新入口

当前决策：

- 这不是 ownership 主线的阻塞项
- 暂时不继续扩大 SWF 改动范围
- SWF 侧仍然先专注于主菜单动态图标与样式
- 等 ownership 主线阶段性落稳后，再逐页补 `DualPad_OnPresentationChanged()` 或同等刷新入口

后续恢复时的建议顺序：

1. 先盘点哪些已 patch SWF 页面真正实现了 presentation-changed 回调
2. 对没有回调的页面，补最小刷新入口
3. 只重刷动态图标控件，不碰页面其它逻辑
4. 从暂停菜单/日志/地图这类高频页面开始
