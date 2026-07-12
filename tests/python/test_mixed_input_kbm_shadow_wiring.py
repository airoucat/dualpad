import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputKbmShadowWiringTests(unittest.TestCase):
    def test_owner_pump_records_observe_produce_publish_boundaries(self) -> None:
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")

        self.assertIn("[DualPad][KbmIngressShadow]", source)
        self.assertIn("KbmIngressDiagnosticSampler", source)
        self.assertIn("BuildKbmIngressFingerprint", source)
        self.assertIn("observedEventCount", source)
        self.assertIn("mappedEdgeCount", source)
        self.assertIn("batchAccepted", source)
        self.assertIn("firstNativeUserEvent", source)
        self.assertIn("firstNativeValue", source)
        self.assertIn("firstNativeHeldDuration", source)
        self.assertIn("GetUserEvent()", source)
        self.assertIn("Value()", source)
        self.assertIn("HeldDuration()", source)

        observe = source.index("ObserveEventList(")
        produce = source.index("BuildIngressBatch(")
        publish = source.index("PublishOwnerKbmBatch(")
        shadow = source.index("[DualPad][KbmIngressShadow]")
        drain = source.index("DrainOnOwnerTick(")
        self.assertLess(observe, produce)
        self.assertLess(produce, publish)
        self.assertLess(publish, shadow)
        self.assertLess(shadow, drain)

    def test_shadow_does_not_enable_or_mutate_gated_capabilities(self) -> None:
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")
        diagnostic = (ROOT / "src/input/injection/KbmIngressDiagnostics.h").read_text(
            encoding="utf-8"
        )

        combined = source + diagnostic
        self.assertNotIn("i0Approved = true", combined)
        self.assertNotIn("ProductionMutationEnabled(true", combined)
        self.assertNotIn("ResolveGamepadDeviceAvailability", combined)
        self.assertNotIn("WriteRelative", diagnostic)
        self.assertNotIn("RE::InputEvent", diagnostic)

    def test_runtime_records_the_same_kbm_facts_after_assembly(self) -> None:
        source = (ROOT / "src/input_v2/gameplay/DualPadRuntime.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("[DualPad][KbmRuntimeShadow]", source)
        self.assertIn("g_kbmRuntimeSampler", source)
        policy = source.index("BuildGameplayPolicyFromFacts(", source.index("BuildStableRuntimeInput"))
        shadow = source.index("[DualPad][KbmRuntimeShadow]")
        result = source.index("return DualPadRuntimeInput", policy)
        self.assertLess(policy, shadow)
        self.assertLess(shadow, result)


if __name__ == "__main__":
    unittest.main()
