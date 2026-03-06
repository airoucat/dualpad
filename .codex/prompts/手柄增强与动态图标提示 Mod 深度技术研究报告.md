# Skyrim SE/AE 手柄增强与动态图标提示 Mod 深度技术研究报告

## 结论摘要
- 采用“B 主 A 辅”在你的目标集（扩展输入语义 + 上下文图标替换 + 不改 SkyUI 原文件夹）下是**最可维护、最兼容**的路径：核心逻辑放 SKSE 插件（B），SkyUI 侧只做**最小显示承接**（A）并以 Mod 管理器覆盖完成安装与切换。citeturn3search8turn1search1turn5search1turn5search16  
- UI 兼容的关键不是“改一堆菜单 SWF”，而是尽量收敛到**共享资源点**（优先 ButtonArt / 图标库）+ **Scaleform 回调桥**：像 moreHUD 那样以 SKSE 插件注册 Scaleform 函数、在菜单加载时注入/联动 SWF，是社区验证过的低侵入路线。citeturn3search8turn3search1turn1search1turn5search19  
- 输入采集必须区分“游戏原生输入”（InputEvent/ControlMap）与“扩展设备特性输入”（触控板手势/陀螺仪/自适应扳机）：前者走 RE::InputEvent* 事件汇聚，后者以独立 Provider 插件化接入，统一输出为虚拟语义动作（Action）。citeturn2search1  
- 上下文判定不要每帧扫描：改为**事件驱动 + 分层缓存**（菜单开关事件、十字准星目标变化、战斗状态变化），在“上下文切换边界”才触发图标映射刷新，避免 UI/输入抖动与性能浪费。citeturn2search1turn5search3turn2search17  
- 兼容 UI Overhaul（Dear Diary / Untarnished / 各类按钮图标包）的核心矛盾在于它们会覆盖同名 SWF（尤其 ButtonArt），因此发布必须拆分为：Core（SKSE）+ UI Patch（多套 ButtonArt 变体）并在 FOMOD 中选择，且要对 Vortex/MO2 的“谁覆盖谁”给出明确规则。citeturn4search1turn2search2turn2search3turn5search1turn5search16  
- 版本漂移（SE 1.5.x vs AE 1.6.x）与地址库变化是 CTD 高发源：Address Library 已明确分裂为 SE/AE 两套且 ID 不可混用；建议用 CommonLibSSE-NG 多运行时单 DLL + 地址库依赖检查 + 初始化失败即退出，降低“半加载”崩溃。citeturn0search1turn0search4turn3search0  
- controlmap.txt 级别的方案维护成本高且存在“旧版本 controlmap 在新 AE 上可能 CTD”的社区反馈；你的目标是“增强与动态提示”，应避免把核心能力绑定在 controlmap 替换上，只把它当作可选兼容补丁。citeturn4search9turn4search10turn4search0  
- 配置与持久化建议优先用 MCM Helper：它支持把用户设置写入独立 INI（Data\MCM\Settings）并与 Mod 更新隔离，天然适配“核心逻辑在 SKSE、配置在数据层”的维护策略。citeturn0search6  

## 架构与职责边界
**本节覆盖：A 架构蓝图、H 命名与配置规范、以及“哪些放 SKSE / 哪些放 SkyUI / 哪些放配置层”的明确划分。**

### A. 架构蓝图与模块分层
你的目标同时涉及“输入增强”和“UI 动态图标提示”。在 Skyrim 生态里，最稳健的分层是把系统拆成四层：SKSE（运行时内核）、Papyrus（任务/事件薄层）、Scaleform/SWF（显示层）、配置与资源（数据层）。SKSE 插件作为主控是因为：SKSE 能拿到输入事件、菜单生命周期、Scaleform 注入点；而 SWF 侧适合做轻显示与调用桥。citeturn2search1turn1search1turn5search19turn5search3  

建议的模块结构（“B 主 A 辅”落地版）：

**B：SKSE 插件（主）**
- Input Hub：统一接入 RE::InputEvent*（原生按键/摇杆/扳机）事件流，并接入扩展输入 Providers（DSE 扩展键、触控板、陀螺仪）。事件汇聚建议用 RE::BSTEventSink<RE::InputEvent*> 的模式。citeturn2search1  
- Semantic Action Layer：把“物理输入”映射成“语义动作”（Action），例如 `Attack`, `Block`, `MenuPageLeft`, `QuickSlotPrev`, `GyroAim`, `TouchpadSwipeLeft`。  
- Context Engine：维护“上下文”（Context）状态机，输入同一个 Action 在不同 Context 下对应不同图标与（可选）不同执行语义。Context 来源：MenuOpenCloseEvent、CrosshairRefEvent、战斗状态/潜行/骑乘等游戏态事件、以及你自己定义的“手柄模式”（如 GyroAimOn）。citeturn2search1turn2search17  
- Icon Resolver：核心决策模块，根据 `Action + Context + DeviceProfile + IconTheme` 产出 `IconKey`（例如 `ps_dualsense_l2`, `touchpad_swipe_left`, `xbox_rt`）。  
- UI Bridge（Scaleform）：对 SWF 暴露“查询/推送”接口。SKSE 侧通过 ScaleformInterface 注册函数/对象，让 AS2 能调用 `GetIconKey(...)`、`GetGlyphAsset(...)` 等。SKSE 注册回调时能拿到 GFxMovieView 指针及 `skse.plugins` 下的对象用于挂载接口。citeturn1search1turn5search19turn3search8  
- Config Service：加载/热更新 JSON/INI，向 MCM Helper 或自定义菜单提供读写。MCM Helper 允许把设置持久化到 Data\MCM\Settings 并与 Mod 更新隔离，适合存用户映射与主题选择。citeturn0search6  

**A：SkyUI / SWF（辅）**
- ButtonArt Shim（最小补丁）：只在 ButtonArt 或你选定的“统一图标入口”处增加对 SKSE 接口的调用，实现“同一个提示位在不同上下文显示不同图标”。  
- 图标素材（DDS/PNG/内嵌矢量）与字体：按主题包拆分，不把逻辑塞进 SWF。  
- MCM（可选）：只做 UI 配置入口与展示，不做复杂决策。

**Papyrus（薄层，可选）**
- 用于：兼容老 mod 的事件触发、提供 MCM 菜单回调、或在某些无法从 SKSE 可靠探测的情境下手动切换 Context。  
- 避免：在 Papyrus 做“实时输入判定/每帧轮询/复杂映射”，因为会不可控地拉低性能与稳定性（且难调试）。moreHUD 的实践是“多数逻辑在 SKSE + SWF，Papyrus 只做热键与参数传递”，可以作为你项目的架构参照。citeturn3search1turn3search8  

### 数据流与生命周期
推荐“事件驱动”数据流（避免每帧查询），逻辑顺序如下：

1) **输入进入**：原生输入由 InputEvent 进入；扩展输入由 Provider 进入（异步采集但主线程投递）。citeturn2search1turn5search3  
2) **语义化**：转成 Action（独立于具体手柄/键位）。  
3) **上下文更新**：MenuOpenCloseEvent、CrosshairRefEvent、系统消息（如 kInputLoaded / kPostLoadGame）触发 Context Engine 更新与缓存失效。citeturn2search1turn2search17turn1search2  
4) **决策**：Icon Resolver 计算 `Action → IconKey`。  
5) **UI 刷新**：当且仅当（Context 或主题或设备 Profile 变更）时，通过 GFxMovieView 调用或变量注入刷新提示。Scaleform 运行在主线程，UI 更新必须在菜单消息处理/更新回调时完成。citeturn5search3turn1search1  

### H. 命名与配置规范建议
建议你从第一天就把“虚拟键/语义动作/上下文/图标资源键”规范化，否则后期扩展（尤其加入触控板手势、陀螺仪模式）会非常痛苦。

**命名域建议**
- `DeviceProfile`：`xbox_one`, `ps_dualsense`, `ps_dualsense_edge`, `steamdeck`, `generic_xinput`  
- `InputToken`（物理输入标识）：`gp.a`, `gp.rt`, `kb.e`, `tp.swipe_left`, `gyro.yaw`  
- `Action`（语义动作）：`combat.attack`, `combat.block`, `menu.page_left`, `menu.page_right`, `ui.confirm`, `ui.back`, `mode.gyro_aim_toggle`  
- `Context`：`gameplay.explore`, `gameplay.combat`, `menu.inventory`, `menu.magic`, `menu.journal`, `dialogue.choice`, `crafting.smithing`  
- `VKey`（虚拟键/虚拟按钮）：`vkey.primary`, `vkey.secondary`, `vkey.page_prev`, `vkey.page_next`（注意：VKey 不是“哪个物理键”，而是“当前语义槽位”，便于替换图标与映射）  
- `IconKey`：`icon.ps.l2`, `icon.ps.r2`, `icon.tp.swipe_left`, `icon.tp.swipe_right`, `icon.gyro`  

**映射表推荐范式：Action → Context → VKey → IconKey**
- 好处：当你新增输入语义（例如触摸板新手势）时，只要新增 `InputToken → Action` 或新增 `IconKey`，不必重构 UI 层与决策层。

**配置样例（JSON，示意）**
```json
{
  "version": 1,
  "deviceProfiles": {
    "ps_dualsense": {
      "iconTheme": "untarnished",
      "extensions": ["touchpad", "gyro", "adaptive_triggers"]
    }
  },
  "contexts": {
    "menu.inventory": {
      "inherits": ["menu.base"],
      "overrides": {
        "menu.page_left": { "vkey": "vkey.page_prev", "icon": "icon.tp.swipe_left" },
        "menu.page_right": { "vkey": "vkey.page_next", "icon": "icon.tp.swipe_right" }
      }
    },
    "gameplay.combat": {
      "overrides": {
        "combat.attack": { "vkey": "vkey.primary", "icon": "icon.ps.r2" },
        "combat.block": { "vkey": "vkey.secondary", "icon": "icon.ps.l2" }
      }
    }
  },
  "inputBindings": {
    "ps_dualsense": {
      "gp.r2": "combat.attack",
      "gp.l2": "combat.block",
      "tp.swipe_left": "menu.page_left",
      "tp.swipe_right": "menu.page_right",
      "gyro.motion": "aim.delta"
    }
  }
}
```

### 明确分工清单
下面这张“职责矩阵”是落地时最重要的边界，用来避免 A/B 层相互污染：

| 内容 | 应放 SKSE 层（B） | 应放 SkyUI/SWF 层（A） | 应放配置层 |
|---|---|---|---|
| 输入采集（原生 + 扩展） | ✅（InputEvent + Providers）citeturn2search1 | ❌ | 可选（物理键到 Action 的映射表） |
| 上下文判定（菜单/战斗/模式） | ✅（Context Engine）citeturn2search1turn2search17 | ❌ | 可选（上下文规则、优先级） |
| 图标/提示决策（Action→IconKey） | ✅（Icon Resolver） | ❌ | ✅（映射表、主题选择） |
| Scaleform 对接接口 | ✅（注册函数/对象，推送更新）citeturn1search1turn5search19 | ✅（调用接口、渲染） | ❌ |
| 菜单 SWF 修改范围 | ❌ | ✅（尽量只改 ButtonArt/共享入口）citeturn4search1turn2search2turn2search3 | ❌ |
| 用户设置持久化 | 可选（读取/写入） | 可选（MCM展示） | ✅（MCM Helper 的 INI 或 JSON）citeturn0search6 |

## 兼容性与发布策略
**本节覆盖：B 兼容性分析、E 发布策略（Core + UI 补丁包 + FOMOD + MO2/Vortex 规则建议）。**

### B. 与主流 UI/信息类 Mod 的冲突点与规避策略
#### 与 SkyUI 的关系
SkyUI 属于“全局 UI 框架”，大量菜单 SWF 与 MCM 都依赖它。SkyUI SE 在 Nexus 的页面明确其为“界面改造”并依赖 SKSE64 版本匹配。citeturn3search2turn5search6  
你的策略应是：**永远不改 SkyUI 原 Mod 文件夹**，只用 MO2/Vortex 的覆盖机制提供“后加载的补丁文件”。MO2 的设计目标就是通过虚拟文件系统隔离 Mod，保持游戏目录干净，适合你“安装时不手动修改 SkyUI”的诉求。citeturn5search0turn5search16  

#### 与 Dear Diary / Untarnished / Edge UI 等 UI Overhaul 的冲突
这些 UI Overhaul 的本质是“菜单替换器”，会覆盖 SkyUI/原版的多个 SWF。Dear Diary 的说明本身就强调“让其他 mod 覆盖它的文件也能用，只是视觉一致性会丢失”，意味着它预期用户通过覆盖解决冲突。citeturn2search2turn2search18  
Untarnished 明确是基于 Dear Diary Dark Mode 的界面替换。citeturn2search3  
因此你的冲突点集中在两类文件：
- **ButtonArt.swf / 按钮图标库**：大量按钮图标包都靠覆盖 ButtonArt 实现，且 Nexus 上明确写了“buttonart.swf 会覆盖任何改过它的 mod，取决于加载顺序”。citeturn4search1turn1search8  
- **特定菜单 SWF**：如 Journal/Inventory/MessageBox 等在 UI Overhaul 中被“压缩/分辨率适配”补丁频繁修改（Unsquished Fix 的存在说明 UI SWF 细节差异会引发显示问题）。citeturn2search15turn2search2turn2search3  

**规避策略（可执行）**
- 把你的 UI 侵入面收敛到“共享入口”：优先改 ButtonArt（或其等价资源），避免去改每个菜单 SWF。这样你只需要对不同 UI 主题提供不同 ButtonArt 变体即可。citeturn4search1turn2search3  
- UI Patch 以“主题包”形式发布：`UI Patch - SkyUI Vanilla`, `UI Patch - DearDiary`, `UI Patch - Untarnished`, `UI Patch - Nordic/ImGui Icons`。Nexus 上存在专门的“ImGui/SkyUI icon 资源”和“Untarnished 图标补丁”，说明社区生态已习惯用“资源包 + 补丁包”组合。citeturn4search7turn2search19turn2search11  
- 在 FOMOD 中明确：如果用户装了 Dear Diary/Untarnished，则选择对应 UI Patch；否则选择默认 SkyUI Patch。

#### 与 moreHUD / TrueHUD 类 HUD 增强的冲突
moreHUD 的说明非常关键：它的实现“多数在 SKSE 插件和 HUD 的 SWF 文件中，Papyrus 只用于热键与向 widget 传参”，并且其源码强调“插件在 HUD 菜单加载时动态加载 SWF movie clip，并注册 Scaleform 函数给 ActionScript”。citeturn3search1turn3search8  
这给你的兼容性结论：
- 你完全可以采用与 moreHUD 类似的“HUDMenu 注入 + Scaleform 注册”方式，尽量避免覆盖 HUDMenu.swf 本体，从而与 moreHUD 等共存。citeturn3search8turn1search1  
- 冲突仍可能发生在：同一显示层级的命名冲突、重复注入相同实例名、或多个插件都试图覆盖同一个共享 SWF（例如 ButtonArt）。因此你的 SWF 命名必须命名空间化（详见后文风险与规范）。

### E. 发布策略与安装生态适配
你的发布必须按“核心/补丁分离”，否则不可维护。

#### 包结构建议
- **Core（必装）**  
  - `Data/SKSE/Plugins/<YourMod>.dll`  
  - `Data/SKSE/Plugins/<YourMod>.toml|ini|json`（默认配置）  
  - （可选）Papyrus 与 MCM helper 相关文件  
  - 不包含任何会覆盖 UI Overhaul 的 SWF（避免强冲突）

- **UI Patch Packs（可选，互斥/半互斥）**  
  - `interface/xxx/buttonart.swf`（按主题分变体）  
  - `interface/xxx/*.dds`（图标材质）  
  - 其他必要的“最小 SWF 改动”（不要把整套菜单都搬过来）

#### FOMOD 方案（可执行）
- 第一页：运行时与前置检查提示  
  - SKSE64（Steam/GOG）与 Address Library（若你依赖）说明。SKSE64 的兼容说明指出其支持 Steam 与 GOG，其他商店（EGS/Windows Store/Game Pass）不支持；你的安装器应明确提示。citeturn5search6turn5search2  
- 第二页：UI 主题选择（单选）  
  - SkyUI 默认  
  - Dear Diary Dark Mode  
  - Untarnished UI  
  - 其他（Nordic/ImGui Icons 资源）  
- 第三页：输入设备扩展（多选）  
  - DualSense/触控板  
  - 陀螺仪  
  - 自适应扳机  
  - Steam Deck 触控板（如你支持）  
- 第四页：兼容补丁提示（可选）  
  - 如果用户装了按钮图标资源包（例如 Sovngarde UI Buttons 这类会覆盖 buttonart.swf 的资源），告知“以谁为准”的规则。citeturn4search1  

#### MO2 与 Vortex 规则建议（必须写进说明页）
- **MO2**：MO2 通过虚拟文件系统将 mod 叠加到游戏之上，用户通过“左侧 Mod 列表优先级”决定谁覆盖谁；你的文档应明确要求“UI Patch 放在 UI Overhaul 之后（或按你定义的胜出关系）”。citeturn5search0turn5search16  
- **Vortex**：必须区分“插件 Load Order（ESP/ESL）”与“文件覆盖冲突”。Nexus 论坛明确指出：Vortex 的冲突解决处理的是部署时文件覆盖，plugin load order 对文件覆盖没影响。citeturn5search1turn5search13  
  - 因此你应提供两段指引：  
    - 你的 UI Patch 需要在“File Conflicts”里设为覆盖/被覆盖；  
    - 若你有 ESP（最好没有），才需要 LOOT/规则处理。

## 性能与可扩展性方案
**本节覆盖：C 性能分析、D 扩展性设计，并按“稳健优先/开发速度优先”给出两套实现备选与对比表。**

### C. 性能分析与最坏场景评估
#### 事件驱动 vs 每帧查询
- **推荐：事件驱动**。输入本身通过 InputEvent 事件流进入；菜单切换通过 MenuOpenCloseEvent；SKSE 还有系统消息（例如 kInputLoaded、kPostLoadGame）供你在正确时间点初始化。citeturn2search1turn2search17turn1search2  
- **不推荐：每帧查询**，原因不是“绝对做不了”，而是会把性能与稳定性风险扩大到所有玩家环境，同时更容易引发 UI 抖动（例如 Context 在帧间反复判定）。Scaleform 在主线程运行，频繁 UI Invoke/SetVariable 会放大主线程负担。citeturn5search3  

#### 缓存策略（可执行）
建议三层缓存，分别解决不同成本来源：

- **Context Cache**：  
  - key：`(MenuName, GameplayStateFlags, DeviceModeFlags)`  
  - 更新时机：MenuOpenCloseEvent、战斗状态变化、你自定义模式切换（如 GyroAim Toggle）。citeturn2search1  

- **Resolution Cache（Action→IconKey 的解算缓存）**：  
  - key：`(ActionId, ContextId, DeviceProfileId, ThemeId)`  
  - value：`IconKey` +（可选）`AssetPath/FrameLabel`  
  - 失效条件：配置变更、主题变更、设备 profile 变更、context 变更。

- **UI Binding Cache（GFx 对象句柄缓存）**：  
  - 缓存 GFxMovieView 与目标 AS 对象路径（例如 `_root.ButtonArt`、`_root.HUDWidget`），避免每次刷新都用字符串路径查找。  
  - 注意：菜单关闭后对象失效，必须在 MenuOpenCloseEvent close 时清理，防止悬挂指针导致 CTD。Scaleform 对象由游戏分配器管理且在主线程更新，生命周期管理是稳定性关键。citeturn5search3turn2search1  

#### 状态切换成本
- **最坏场景定义**：玩家在战斗中快速打开/关闭物品栏、切换不同菜单页，同时触控板连续滑动触发翻页、陀螺仪持续输出，UI 需要频繁更新提示。  
- **控制手段**：  
  - 对 UI 刷新做“合帧/去抖动”：把连续事件合并到下一次菜单 update tick（例如 16ms~50ms 窗口），只刷新最终状态。  
  - 为陀螺仪/触控板类高频信号设置“采样→语义事件”的门槛：UI 提示只关心“模式开/关”或“手势识别结果”，不关心每个采样点。  
  - 将“自适应扳机反馈”作为输出通道单独调度，不与 UI 同频更新（否则会把主线程的 UI 与外设 IO 耦合）。

### D. 扩展性设计
扩展性关键是：新增输入语义不要求你去改 SWF 或重写 Context Engine。

#### 采用“Provider + Action + Resolver”插件化骨架
- **Input Provider 接口**（概念）：每类设备/特性一个 Provider，输出统一的 `InputToken` 事件。  
  - `VanillaProvider`：从 RE::InputEvent* 解析出 `gp.* / kb.*`。citeturn2search1  
  - `TouchpadProvider`：识别 `tp.swipe_left/right`、`tp.zone_click_*`。  
  - `GyroProvider`：识别 `gyro.motion`、`gyro.mode_toggle`。  
  - `TriggerProvider`：输出 `trigger.pull_depth` 等（若你做半按/全按不同语义）。

- **Action Registry**：集中注册“语义动作”，并用配置把 InputToken 绑定到 Action。  
- **Context Engine**：只关心“当前处于哪个 Context”与“哪些 Context 覆盖哪些 Action 的 IconKey”，不关心设备细节。  
- **Icon Resolver**：只做“按规则解算”，规则由配置驱动。

#### 对 UI 层的扩展约束
- UI 层只认识 `IconKey`，不认识“触控板/陀螺仪是什么”。  
- 新增输入语义时：你只需新增 `IconKey` 的素材（或帧标签）+ JSON 里新增规则；SWF 不需要随之增长逻辑复杂度。

### 两套实现备选与对比
你要求“至少两套实现备选（稳健优先/开发速度优先）”，这里给出可实际落地的两条路径：

#### 备选方案一：稳健优先
核心思想：**SKSE 主控 + Scaleform 桥 + 最小 ButtonArt Shim**，并尽量用注入方式避免覆盖大 SWF。

- UI 侧：只维护（按主题）少数共享 SWF（典型是 ButtonArt）与图标资源。按钮图标包会覆盖 buttonart.swf，加载顺序决定胜者，因此你以“主题变体”方式提供兼容。citeturn4search1  
- SKSE 侧：注册 Scaleform 接口，让 ButtonArt 在需要显示提示时向 SKSE 查询当前 IconKey。SKSE 注册回调可获取 GFxMovieView 与插件对象挂载点，这是 SKSE Scaleform 对接的标准模式。citeturn5search19turn1search1  
- HUD 注入参考 moreHUD：在 HUD 菜单加载时动态注入你的 movie clip（如果你需要 HUD 层额外提示），以避免直接覆盖 HUDMenu.swf。citeturn3search8turn3search1  

优点：兼容性与维护性最佳；缺点：前期需要你把 Scaleform 桥打磨得很稳。

#### 备选方案二：开发速度优先
核心思想：**直接覆盖你关心的菜单 SWF + 在 AS 侧做更多逻辑**，快速做出效果。

- UI 侧：为 Inventory/Journal/Magic 等菜单各提供改过的 SWF，把“上下文判断”与“图标替换”部分逻辑写在 AS2。  
- SKSE 侧：只做输入增强与少量数据传递，不做复杂 Icon Resolver。

优点：PoC 很快；缺点：与 Dear Diary/Untarnished 等会产生大量 SWF 冲突（它们本来就是菜单替换器），后续合并成本极高。citeturn2search2turn2search3turn2search18  

#### 对比表
| 维度 | 稳健优先：ButtonArt Shim + SKSE Resolver | 速度优先：覆盖多菜单 SWF |
|---|---|---|
| 可维护性 | 高：改动集中、规则数据化 | 低：SWF 分裂、维护面大 |
| 与 SkyUI 兼容 | 高：最小侵入、易跟随 SkyUI 更新 | 中：容易被 SkyUI/补丁覆盖冲掉 |
| 与 Dear Diary/Untarnished 兼容 | 中到高：做主题变体即可 | 低：大量 SWF 冲突不可避免citeturn2search2turn2search3 |
| 性能 | 高：事件驱动 + 缓存，主线程 Invoke 可控 | 中：AS 逻辑膨胀且刷新频繁 |
| 扩展新输入语义 | 高：新增 Provider + 配置 | 中到低：AS 与多 SWF 都要改 |
| 发布复杂度 | 中：Core + 多 UI Patch + FOMOD | 高：大量补丁组合爆炸 |
| 推荐阶段 | 长期维护、面向大众发布 | 仅限内部 PoC/小圈子 |

## 风险清单与回退策略
**本节覆盖：F 风险清单（CTD、版本漂移、地址库、UI 覆盖冲突、降级/回退方案）。**

### F. 主要风险与可执行缓解措施
#### CTD 风险
- **悬挂的 GFx 引用/跨线程操作 UI**：Scaleform/UI 在主线程，且对象由游戏内存管理；如果你缓存 GFx 指针但菜单已关闭，会出现访问非法内存风险。缓解：严格在 MenuOpenCloseEvent close 时清理 UI Binding Cache；所有 UI Invoke 都安排在菜单更新回调/主线程时机。citeturn5search3turn2search1  
- **Hook/Trampoline 使用不当**：CommonLibSSE 的 Relocation/Trampoline 系统用于跨版本定位与安全写入跳板，错误 hook 是典型 CTD 源。缓解：把 hook 点最小化、只在初始化时安装、所有地址解析失败则让插件初始化失败（不要“半工作”）。citeturn3search0  

#### 版本漂移（SE/AE）与地址库变化
- Address Library 明确提示：已分为 SE(1.5.x) 与 AE(1.6.x) 两套，ID 不可互配，且两边函数代码差异大。缓解：  
  - 若你依赖地址库：在启动时检测 Address Library 版本是否匹配当前 runtime，否则直接报错退出；  
  - 尽量使用 CommonLibSSE-NG 的多运行时支持，减少你为不同 runtime 发布多 DLL 的负担。citeturn0search1turn0search4turn0search0  

#### UI 覆盖冲突
- ButtonArt 等共享 SWF 的覆盖冲突是“必然会发生”的生态现实：资源包也明确说明会覆盖其他修改过它的 mod，取决于加载顺序。缓解：  
  - 把 UI Patch 拆成独立 mod；  
  - 说明文档中把“谁覆盖谁”的优先级写死，并给出 MO2/Vortex 的操作方式；  
  - 提供一键回退：只禁用 UI Patch，Core 仍可工作（只是图标提示回到默认）。citeturn4search1turn5search1turn5search16  

#### controlmap.txt 相关风险
- 社区与教程层面存在“旧版本 controlmap.txt 在新 AE 中可能 CTD”的提醒，且 controlmap 修改本身对格式敏感。缓解：  
  - 不把 controlmap 当核心依赖；  
  - 若你提供 controlmap 预设，必须按 AE 版本分发并在 FOMOD 做显著警告。citeturn4search9turn4search0turn4search10  

### 回退/降级方案设计
你应在架构层预置“降级开关”，保证玩家遇到 UI 冲突时能自救：

- **UI Patch 可完全移除**：Core 仍能提供输入增强（只是提示不完美）。  
- **Icon Resolver 失败回退**：如果查询不到 IconKey，则返回“默认手柄图标”（L2/R2/Xbox RT 等）或 SkyUI 原提示。  
- **扩展输入 Provider 可单独禁用**：例如触控板驱动在某些设备上不稳定时，不影响原生输入。  
- **日志与诊断**：在 SKSE log 中输出“当前 runtime、地址库检测结果、已启用的 UI Patch 主题、冲突提示（检测到 ButtonArt 来源）”，帮助用户定位覆盖问题。

## 里程碑路线图与交付计划
**本节覆盖：G 里程碑路线图（PoC→Beta→Release）、最后的“决策结论 I”、以及你要求的“90 天执行计划（按周）”与“上线前检查清单”。**

### G. 里程碑路线图与验收标准
#### PoC 阶段
**目标**：证明“同一个动作在不同上下文显示不同图标”全链路可用，并完成至少一种扩展输入（触控板左右滑）映射到菜单翻页提示。

可验收目标：
- 在 `menu.inventory` 中把翻页提示从 LT/RT 动态替换为 `tp.swipe_left/right` 图标；退出菜单回到战斗时，攻击提示仍显示 L2/R2。  
- Context Engine 能正确识别“菜单内/外”并在切换时只刷新一次 UI（无抖动）。  
- UI Patch 只修改一个共享入口（如 ButtonArt）即可生效，不覆盖整套菜单 SWF。  
- SKSE 插件能通过 Scaleform 接口向 SWF 提供图标键查询（证明桥可用）。citeturn1search1turn5search19turn5search3  

测试清单：
- 打开/关闭物品栏、魔法、日志菜单多次，观察提示刷新是否正确。  
- 连续快速切换菜单页（高频手势），确认无卡顿与无崩溃。  
- 仅启用 SkyUI 与你的 Core + UI Patch，验证最小环境稳定。citeturn3search2  

#### Beta 阶段
**目标**：扩展到多上下文、多主题，并引入 MCM 配置可用性与持久化。

可验收目标：
- 支持至少 6 个 Context：`gameplay.explore/combat` + `menu.inventory/magic/journal/map`。  
- 支持至少 2 套 UI Patch：SkyUI 默认 + Untarnished（或 Dear Diary）。Untarnished/ Dear Diary 都是菜单替换器生态核心，应优先覆盖。citeturn2search2turn2search3  
- MCM 可修改：触控板翻页开关、GyroAim 模式开关、图标主题选择；并且设置能持久化在独立配置文件中（MCM Helper 的典型能力）。citeturn0search6  
- 提供 Vortex/MO2 的覆盖指导文档（包含截图/路径说明）。

测试清单：
- 与 moreHUD 共存：确认两者同时注入 HUD 时不互相覆盖（命名不冲突）。citeturn3search1turn3search8  
- 与 Dear Diary/Untarnished 共存：切换 UI Patch 胜出方后提示仍工作。  
- 低配置/高 Mod 数环境压力测试：进出菜单 30 次、切换快速旅行/载入存档，观察日志无错误累积。

#### Release 阶段
**目标**：面向大众发布，确保版本漂移、覆盖冲突、降级路径都有兜底。

可验收目标：
- FOMOD：Core + 可选 UI Patch 包，选择逻辑清晰；安装后无需手工改 SkyUI 文件夹。  
- 插件初始化严格校验：runtime、SKSE、（可选）地址库缺失或不匹配则明确提示并拒绝加载，避免不确定状态。citeturn0search1turn5search6turn3search0  
- 文档包含：MO2/Vortex 覆盖说明（特别强调 Vortex 的“文件覆盖”和“插件 load order”区别）。citeturn5search1turn5search13  
- 崩溃回滚方案：禁用 UI Patch、禁用扩展 Provider、恢复默认主题均可正常运行。

### I. 决策结论：为什么 B 主 A 辅优于纯 A 或纯 B
#### 为什么不做“纯 A（全靠 SWF/SkyUI 改造）”
- 你需要的输入语义（触控板手势、陀螺仪、自适应扳机）并非 SkyUI 原生能力，SWF/AS2 无法可靠直接访问外设特性；最终还是要回到 SKSE/C++ 做采集与判定。  
- 纯 A 必然导致你修改大量菜单 SWF，直接与 Dear Diary/Untarnished 等菜单替换器正面冲突（它们本质就是靠覆盖一堆 SWF 实现）。citeturn2search2turn2search3turn2search18  
- 因此纯 A 的维护成本会指数级增长：每次 UI Overhaul 更新、每次分辨率修复补丁（Unsquished Fix）都可能让你的 SWF 合并地狱重来。citeturn2search15  

#### 为什么不做“纯 B（全靠 SKSE，不改/不补 SWF）”
- UI 图标提示最终在 Scaleform/SWF 渲染，若 SWF 完全不配合，你很难让既有提示位“按上下文换图标”。SKSE 可以注入变量/调用函数，但要让 UI 的某个组件改用你的逻辑，通常需要一个最小的 SWF 入口去调用 SKSE 的接口（moreHUD 的实践同样是“SKSE + SWF 配合”）。citeturn3search8turn3search1turn5search19  

#### B 主 A 辅的取舍条件
在你的场景下，B 主 A 辅成立的前提（满足越多越应该选它）：
- 你需要接入**非原生输入特性**，必须有 SKSE 运行时代码（满足）。  
- 你希望**兼容多 UI 主题**且不手改 SkyUI 文件夹，只靠覆盖与排序（满足，且 MO2/Vortex 正好支持这种覆盖控制）。citeturn5search16turn5search1  
- 你能接受“UI 必须提供少量补丁文件（ButtonArt shim）”但不想维护几十个菜单 SWF（满足）。  

如果出现以下情况才考虑偏向“更 A”：
- 你的目标 UI 范围极窄（只改一个特定菜单），且你明确不支持 UI Overhaul；  
- 或你只做“换一套图标皮肤”，没有动态上下文决策——那可以走纯 A 资源包路线（类似按钮图标资源 mod）。citeturn4search1turn2search19  

### 九十天执行计划
以你当前“B 主 A 辅”路线为前提，按周给出可交付计划（12 周 ≈ 84 天，最后预留缓冲到 90 天）。

**第一个月（周一到周四）聚焦打通链路**
- 第一周：确定 Action/Context/VKey/IconKey 命名规范与 JSON Schema；搭建 SKSE 插件骨架（CommonLibSSE-NG 工程结构）、日志、配置加载；订立 UI Patch 的“只改 ButtonArt”原则。citeturn0search4turn3search0turn2search1  
- 第二周：完成 InputEvent 汇聚与基础 Context（菜单内/外）判定；实现 Icon Resolver 的最小规则（硬编码也行，但接口必须数据化）；定义 Scaleform API 草案（GetIconKey 等）。citeturn2search1turn1search1turn5search19  
- 第三周：制作第一版 ButtonArt Shim（SkyUI 默认主题）并接入 Scaleform 查询；实现“菜单翻页提示 ↔ 触控板左右滑图标”的 PoC；输出 PoC 视频/录屏与日志。citeturn4search1turn3search2  
- 第四周：加入触控板手势 Provider（至少左右滑 + 分区点击中的一种）；实现 UI 刷新去抖动与缓存；PoC 验收与技术债清理。

**第二个月聚焦 Beta：多上下文 + 多主题 + MCM**
- 第五周：扩展 Context 到 Inventory/Magic/Journal；建立 Context 继承与优先级；新增至少 20 个 Action 的映射条目（仍可先只覆盖常用提示）。  
- 第六周：做 Untarnished 或 Dear Diary 的 UI Patch 变体（只改 ButtonArt shim + 素材适配）；内部验证 UI Overhaul 下仍可动态替换。citeturn2search2turn2search3  
- 第七周：接入 MCM Helper（或等价持久化方案），实现用户重映射与主题选择保存；验证 Data\MCM\Settings 的持久化行为与升级安全性。citeturn0search6  
- 第八周：兼容性回归：与 moreHUD 同装、与至少 1 个按钮图标资源包同装（测试覆盖规则）；完善 Vortex/MO2 安装说明与常见冲突 FAQ。citeturn3search1turn3search8turn4search1turn5search1turn5search16  

**第三个月聚焦 Release：稳定性、回退、打包与测试矩阵**
- 第九周：加入陀螺仪模式（最少实现“模式开关 + 图标提示同步”）；把高频信号从 UI 刷新路径剥离；实现 Provider 可单独禁用。  
- 第十周：加入自适应扳机（至少做“输出通道开关 + 与上下文绑定的强度配置”）；压力测试（长时间战斗/菜单反复切换/频繁载入存档）。  
- 第十一周：FOMOD 打包：Core + UI Patch packs；写清“覆盖规则”；加入按 runtime 的提示与（可选）地址库检查策略。citeturn0search1turn5search6turn5search1  
- 第十二周：发布候选（RC）：全面回归测试、崩溃日志收集、文档定稿；准备 Nexus 发布页与版本号策略（语义化版本）。

**剩余缓冲（到 90 天）**
- 修复 Beta 反馈、补齐缺失的菜单上下文、增加 1~2 套额外 UI Patch 主题或图标资源适配（视需求）。

### 上线前检查清单
稳定性与兼容性（必须全部通过）：
- SKSE64 启动环境：确认支持 Steam/GOG；对不支持平台明确提示（EGS/Windows Store/Game Pass）。citeturn5search6turn5search2  
- 初始化健壮性：地址解析失败/版本不匹配时插件拒绝加载，不能“半工作”。citeturn0search1turn3search0  
- UI 生命周期：MenuOpenCloseEvent close 后不保留 GFx 对象引用；所有 UI Invoke 在主线程时机执行。citeturn2search1turn5search3  
- 覆盖冲突：UI Patch 独立可禁用；禁用后 Core 不崩溃、输入增强仍可用。  
- MO2 说明：明确“左侧优先级决定文件覆盖”，并给出推荐排序。citeturn5search0turn5search16  
- Vortex 说明：明确“File Conflicts 与 Plugin Load Order 分离”，并给出如何设置文件冲突胜者。citeturn5search1turn5search13  

功能验收（建议作为发布门槛）：
- 至少 6 个 Context 下提示正确；菜单内外同一键位显示不同图标的例子必须稳定复现。  
- 触控板手势：左右滑稳定识别且不会在游戏外（战斗）误触发菜单动作。  
- 陀螺仪模式：开/关与图标提示同步；模式切换不引发 UI 抖动。  
- 自适应扳机：输出开关可配置；禁用输出后不影响输入映射。  
- MCM：设置可保存并在重启/换存档后保持（推荐采用 MCM Helper 的持久化机制）。citeturn0search6  

包体与文档（发布质量）：
- FOMOD：Core 与 UI Patch 分离；UI Patch 主题互斥关系清晰。  
- 清晰列出已测试的 UI Overhaul 版本（Dear Diary / Untarnished 等），并标注“如果你用其他按钮图标资源包，谁应覆盖谁”。citeturn2search2turn2search3turn4search1  
- FAQ 覆盖：提示不显示、图标错乱、Vortex 规则设置、MO2 覆盖顺序、以及如何回退到默认。