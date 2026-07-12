import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp2BoundaryTests(unittest.TestCase):
    def test_core_kbm_producer_has_no_skyrim_dependency(self) -> None:
        for relative in (
            "src/input_v2/ingress/KbmGameplayFactProducer.h",
            "src/input_v2/ingress/KbmGameplayFactProducer.cpp",
        ):
            contents = (ROOT / relative).read_text(encoding="utf-8")
            self.assertNotIn("<RE/", contents)
            self.assertNotIn("RE::", contents)

    def test_skyrim_adapter_is_the_platform_boundary_and_raw_provider_is_no_go(self) -> None:
        header = ROOT / "src/input/injection/SkyrimKbmInputAdapter.h"
        implementation = ROOT / "src/input/injection/SkyrimKbmInputAdapter.cpp"
        self.assertTrue(header.is_file())
        self.assertTrue(implementation.is_file())

        contents = header.read_text(encoding="utf-8") + implementation.read_text(encoding="utf-8")
        self.assertIn("RE::InputEvent", contents)
        self.assertIn("CaptureBindingSnapshot", contents)
        self.assertIn("ObserveEventList", contents)
        self.assertIn('"Game.Look"', contents)
        self.assertIn("button->IsDown()", contents)
        self.assertIn("physicalOnlyProvenance = false", contents)
        self.assertIn("complete = false", contents)

    def test_input_pump_publishes_owner_kbm_batch_before_drain(self) -> None:
        contents = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")

        for required in (
            "CaptureBindingSnapshot(",
            "ObserveEventList(",
            "BuildIngressBatch(",
            "PublishOwnerKbmBatch(",
            "NextEventBatchToken(",
        ):
            self.assertIn(required, contents)
        self.assertNotIn("PublishKeyboardMouseEvidence", contents)
        self.assertIn("if (bindings.complete && observed.eventListComplete)", contents)

        begin = contents.index("BeginFrame()")
        observe = contents.index("ObserveEventList(")
        publish = contents.index("PublishOwnerKbmBatch(")
        drain = contents.index("DrainOnOwnerTick(")
        self.assertLess(begin, observe)
        self.assertLess(observe, publish)
        self.assertLess(publish, drain)


if __name__ == "__main__":
    unittest.main()
