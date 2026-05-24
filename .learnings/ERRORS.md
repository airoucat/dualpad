# Errors Log

Command failures, exceptions, and unexpected behaviors.

---

## ERR-20260524-001

- Logged: 2026-05-24 10:45 CST
- Priority: medium
- Status: resolved
- Area: xmake / input_v2 target wiring
- Summary: Adding PH4 graph publication to `ActionManifestPublisher` broke `DualPadReplayHarness` linking until PH4 sources were added to every target that consumes `ph1_manifest_compiler_files`.
- Detail: `DualPad` built because it compiles `src/**.cpp`, but focused/replay targets use manual source lists. When a PH1 source starts depending on a new PH4 source, update focused tests and `replay_runtime_files` together.
- Related files: `xmake.lua`, `src/input_v2/config/ActionManifestPublisher.cpp`, `src/input_v2/actions/CompiledActionGraph*.{h,cpp}`
- Resolution: Added `ph4_action_graph_files` to PH1-consuming focused targets and replay runtime lists.
