# Learnings

Corrections, insights, and knowledge gaps captured during development.

**Categories**: correction | insight | knowledge_gap | best_practice
**Areas**: frontend | backend | infra | tests | docs | config
**Statuses**: pending | in_progress | resolved | wont_fix | promoted | promoted_to_skill

## Status Definitions

| Status | Meaning |
|--------|---------|
| `pending` | Not yet addressed |
| `in_progress` | Actively being worked on |
| `resolved` | Issue fixed or knowledge integrated |
| `wont_fix` | Decided not to address (reason in Resolution) |
| `promoted` | Elevated to CLAUDE.md, AGENTS.md, or copilot-instructions.md |
| `promoted_to_skill` | Extracted as a reusable skill |

## Skill Extraction Fields

When a learning is promoted to a skill, add these fields:

```markdown
**Status**: promoted_to_skill
**Skill-Path**: skills/skill-name
```

Example:
```markdown
## [LRN-20250115-001] best_practice

**Logged**: 2025-01-15T10:00:00Z
**Priority**: high
**Status**: promoted_to_skill
**Skill-Path**: skills/docker-m1-fixes
**Area**: infra

### Summary
Docker build fails on Apple Silicon due to platform mismatch
...
```

---

## [LRN-20260523-001] best_practice

**Logged**: 2026-05-23T21:15:00+08:00
**Priority**: high
**Status**: promoted
**Area**: infra

### Summary
本仓库变更完成并验证后，默认提交并推送到当前跟踪远端分支。

### Detail
用户明确要求在本仓库记住该工作流规则：完成变更后默认推送远端。已提升到 `AGENTS.md` 的工作规则；除非用户明确要求只保留本地改动、不提交或不推送，否则后续收尾应包含 commit + push。

## [LRN-20260613-001] best_practice

**Logged**: 2026-06-13T00:08:24+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 实机链路中，底层 HID 有输入但 authoritative poll 仍为零时，不得用菜单 gate 或输出层条件补丁止血，必须逐边界追踪 ingress、runtime projection 与 native commit 的合同。

### Detail
本轮用户明确指出上一版修法像临时止血。实际根因链路也验证了这个风险：source evidence marker 与 source snapshot 分开入队会被其他设备 evidence 交错，导致 `FrameAssembler` 看到较旧 revision 后 hard reset；同时 native commit 若使用 input_v2 `contextRevision` 作为 `contextEpoch`，会与 `ContextResolver` 的 `legacyContextEpoch` 不一致，菜单态下 slot 可能立刻被判 stale。后续处理类似 “有 processed HID 输入但 poll/commit 为零” 的问题时，应先要求或打开 `RuntimePlan`、`NativeButtonCommit`、`runtime_debug_snapshot.csv` 等边界证据，再改边界合同，而不是在最终输出处加特殊 case。

## [LRN-20260613-002] insight

**Logged**: 2026-06-13T00:23:19+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 的 `gateAware` 只应表达 gameplay ownership gate，不应让 Menu / Favorites / Console 等 context-local native output 在 poll commit 层等待 gate。

### Detail
本轮继续审计发现，`Menu.ScrollDown` 等 repeat native actions 的 descriptor 是 `gateAware=true`，但 `NativeButtonCommitBackend` 旧的 `IsGameplayGateOpen()` 对 `InputContext::Menu` 返回 false。结果是 projection 生成 menu repeat command 后，`PollCommitCoordinator` 仍会停在 waiting gate，XInput poll 读到的 digital mask 可能继续为 0。后续改 native routing 时要把“gameplay keyboard/mouse ownership suppression”和“context-local native output 是否允许提交”分开；菜单态本地 native output 不应被 gameplay gate 拦截。

## [LRN-20260614-001] insight

**Logged**: 2026-06-14T20:58:00+08:00
**Priority**: high
**Status**: pending
**Area**: backend

### Summary
DualPad 启动/首次进入 Menu 时，当前帧的 source evidence 必须优先于默认或历史 `PublishedGameplayPresentation.menuEntryOwner`。

### Detail
实机调试中旧日志已经证明 HID/parser 可以读到有效手柄状态，但菜单兼容面和 glyph owner 仍可能保持 KeyboardMouse。`PresentationProjection` 原逻辑在 `_published.uiContextId == None && hostMode == Menu` 时先继承 `menuEntryOwner`，即使同一帧已有 `gamepadEvidence/gamepadLease`。启动主菜单没有 gameplay owner 历史时，默认 `menuEntryOwner=KeyboardMouse` 会压过当前手柄证据。修复时应把非 gameplay 的当前 source evidence 放在 menu-entry inheritance 之前；只有没有新证据时才继承 gameplay menu entry owner。同时 live trace 的 `expected_presentation_surface.csv` 要写实际 committed compatibility surface，不能继续用硬编码 KeyboardMouse 当现场证据。

## [LRN-20260615-001] insight

**Logged**: 2026-06-15T00:13:00+08:00
**Priority**: medium
**Status**: pending
**Area**: build

### Summary
DualPad 本地验证不要假设 CMake 在 PATH；优先按仓库 CI 使用 `xmake build -y <target>` / `xmake run -y <target>`。

### Detail
本轮红灯验证时先尝试 `cmake --build ...`，本机 PowerShell PATH 没有 `cmake`。随后又把 `xmake -y build <target>` 写成了错误顺序，xmake 将 target 解析为 invalid argument。仓库事实入口是 `scripts/ci/run_phase8_ci.ps1`，其中 target 构建/运行格式固定为 `xmake build -y DualPadIngressTests` 与 `xmake run -y DualPadIngressTests`。后续 focused 验证应先看 CI 脚本里的实际命令。
