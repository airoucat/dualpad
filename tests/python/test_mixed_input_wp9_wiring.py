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


if __name__ == "__main__":
    unittest.main()
