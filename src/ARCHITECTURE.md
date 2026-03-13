# DualPad Input Architecture

Current architecture notes are split into three documents:

- `docs/native_button_experiment_postmortem.md`
  - why the consumer-side native button experiment is not the final design
- `docs/final_native_state_backend_plan.md`
  - the new target architecture and migration phases
- `docs/backend_routing_decisions.md`
  - product-facing backend ownership rules from the Route B / keyboard-backend discussion
- `docs/action_contract_output_experiment_zh.md`
  - experimental notes for the actionId -> contract -> output-backend model on the keyboard-native branch
- `docs/unified_action_lifecycle_model_zh.md`
  - backend-neutral lifecycle model intended to be shared by ButtonEvent, keyboard, plugin, and mod routes
- `docs/keyboard_native_backend_plan.md`
  - the current rollback-safe keyboard-native call-site route and its scope
- `docs/native_input_reverse_targets.md`
  - historical reverse-engineering notes for `PollInputDevices` and related call-sites
- `docs/upstream_native_state_reverse_targets.md`
  - the next real reverse-engineering target for the upstream native-state backend
- `docs/upstream_xinput_experiment_plan.md`
  - the current official SE 1.5.97 upstream XInput runtime route and its tradeoffs

Current rule of thumb:

- keep the official `poll-xinput-call` route as the primary SE 1.5.97 runtime path
- keep the keyboard-native route rollback-safe and disabled by default until semantics are verified
- keep the existing XInput compatibility path as the stable fallback
- do not resume `ControlMap`-side native button splicing as the main implementation
- move future work toward a context-aware action planner plus an upstream native-state backend
- keep the gamepad backend narrow and analog-focused
- move most digital control ownership toward a keyboard-native backend

Current code skeleton for that direction lives under `src/input/backend/`:

- `ActionLifecycleBackend.h`
- `ButtonEventBackend.*`
- `ActionBackendPolicy.*`
- `FrameActionPlan.h`
- `FrameActionPlanner.*`
- `NativeControlCode.h`
- `NativeStateBackend.*`
- `PluginActionBackend.*`
- `ModEventBackend.*`
- `VirtualGamepadState.h`

The current processor also builds the new action plan in shadow mode each frame:

- it does not change runtime output yet
- it exists to validate planner/native-state shape before the upstream hook is found
