from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class MixedInputWp9WiringTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_unapproved_identity_disables_legacy_combined_patch_group(self) -> None:
        router = self.read("src/input_v2/presentation/SkyrimEngineModeRouter.cpp")
        compat = self.read("src/input_v2/presentation/SkyrimCompatibilitySurface.cpp")
        self.assertIn(".i0Approved = false", router)
        self.assertIn('addSite("is_using_gamepad_entry"', compat)
        self.assertNotIn('addSite("gamepad_cursor_entry"', compat)
        self.assertNotIn('addSite("gamepad_enabled_vfunc"', compat)

    def test_unscoped_gateway_is_original_first(self) -> None:
        router = self.read("src/input_v2/presentation/SkyrimEngineModeRouter.cpp")
        compat = self.read("src/input_v2/presentation/SkyrimCompatibilitySurface.cpp")
        self.assertIn("return originalGateway ? originalGateway() : false;", router)
        self.assertIn("CallOriginalIsUsingGamepad(self)", compat)
        self.assertNotIn("GetCommittedState().owner == PresentationOwner::Gamepad", compat)
        self.assertNotIn("return true;\n    }\n\n    bool SkyrimCompatibilitySurface::ShouldRefreshMenus", compat)

    def test_runtime_engine_shadow_remains_original(self) -> None:
        runtime = self.read("src/input_v2/gameplay/DualPadRuntime.cpp")
        projection = self.read("src/input_v2/gameplay/EngineModeProjection.cpp")
        self.assertIn("PublishOriginalEngineModeShadow", runtime)
        self.assertIn("EngineInputMode::Original", projection)
        self.assertIn("EngineDecisionCausality::OriginalOnly", projection)

    def test_unapproved_i0_collects_identity_before_any_patch_preparation(self) -> None:
        compat = self.read("src/input_v2/presentation/SkyrimCompatibilitySurface.cpp")
        probe = compat.index("BuildEngineHookIdentityProbe")
        unapproved_gate = compat.index("if (!identityManifest.i0Approved)", probe)
        gateway = compat.index(
            "const auto usingGateway = AllocateEntryGateway", unapproved_gate
        )
        transaction = compat.index(
            "const auto transaction = input::patching::ExecutePatchTransaction", gateway
        )
        self.assertLess(probe, unapproved_gate)
        self.assertLess(unapproved_gate, gateway)
        self.assertLess(gateway, transaction)
        self.assertIn("[DualPad][SkyrimCompat][I0Probe]", compat)

    def test_data_loaded_rechecks_the_live_gamepad_device_identity(self) -> None:
        main = self.read("src/main.cpp")
        data_loaded = main.index("kDataLoaded")
        probe = main.index("RecordI0RuntimeIdentityProbe", data_loaded)
        frame_pump = main.index("InputFramePump::GetSingleton().Register()", probe)
        self.assertLess(data_loaded, probe)
        self.assertLess(probe, frame_pump)

    def test_i0_availability_shadow_samples_the_existing_native_poll_callsite(self) -> None:
        upstream = self.read("src/input/injection/UpstreamGamepadHook.cpp")
        dispatcher = self.read("src/input/injection/PadEventSnapshotDispatcher.cpp")
        serialize = upstream.index("FillSyntheticXInputState")
        fingerprint = upstream.index("BuildI0AvailabilityFingerprint", serialize)
        sample = upstream.index("g_i0AvailabilitySampler.Observe", fingerprint)
        log = upstream.index("[DualPad][I0Availability]", sample)

        self.assertLess(serialize, fingerprint)
        self.assertLess(fingerprint, sample)
        self.assertLess(sample, log)
        self.assertIn("nativePollReached=true", upstream)
        self.assertIn("compatPatchGroup=disabled_i0_no_go", upstream)
        self.assertIn("state->gamepad.buttons", upstream)
        self.assertIn("state->gamepad.thumbLX", upstream)
        self.assertIn("state->gamepad.thumbRY", upstream)
        self.assertIn("state->gamepad.leftTrigger", upstream)
        self.assertIn("state->gamepad.rightTrigger", upstream)
        self.assertIn("BuildI0AvailabilityStateClass", upstream)
        self.assertNotIn(".packetNumber = state->packetNumber", upstream)
        self.assertIn("outputFrame->remapMode", upstream)
        self.assertIn("remapMode={}", upstream)
        self.assertIn("RE::MenuControls::GetSingleton()", dispatcher)
        self.assertIn("GetRuntimeData().remapMode", dispatcher)
        self.assertIn(".remapMode = remapMode", dispatcher)
        self.assertIn("GetGamepadConnectionSnapshot()", dispatcher)
        self.assertIn("GetRuntimeData().currentPCGamePadDelegate", dispatcher)
        self.assertIn("connected={}", upstream)
        self.assertIn("delegateReady={}", upstream)
        self.assertNotIn("MenuControls::GetSingleton", upstream)
        self.assertNotIn("ResolveGamepadDeviceAvailability", upstream)


if __name__ == "__main__":
    unittest.main()
