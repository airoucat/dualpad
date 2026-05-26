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

## ERR-20260526-001

- Logged: 2026-05-26 23:49 CST
- Priority: high
- Status: resolved
- Area: PH8a runtime closeout / replay coverage
- Summary: PH8a was marked completed before `DualPadRuntime` continuously published the stable-frame presentation surface and before transition-frame recovery was proven by targeted tests.
- Detail: Runtime closeout requires proof of the public surface pipeline (`PresentationProjection -> SkyrimCompatibilitySurface -> PromptRuntimeOwner`), transition frames bypassing gameplay projection/output apply, and mandatory replay coverage staying at 10 phase0 scenarios. A `no diff` result over fewer generated scenarios is not a valid closeout signal.
- Related files: `src/input_v2/gameplay/DualPadRuntime*.{h,cpp}`, `src/input_v2/actions/InteractionEngine.cpp`, `tests/input_v2/InputV2Tests.cpp`, `tests/input_v2/ReplayTests.cpp`, `.dualpad-builder/progress.md`
- Resolution: Added targeted runtime surface/recovery tests, fixed multi-axis value binding resolution, restored processor replay coverage to 10 scenarios, and recorded the full verification matrix before re-marking PH8a completed.
