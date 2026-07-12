import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp7RecoveryWiringTests(unittest.TestCase):
    def test_owner_pump_applies_recovery_before_observing_current_callback(self):
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")

        consume = source.index("InputRecoveryMailbox::GetSingleton().ConsumeAll()")
        quarantine = source.index("_kbmProducer.EnterQuarantine", consume)
        reset_synthetic = source.index("_kbmProducer.ResetSyntheticSuppression", consume)
        capture_bindings = source.index("CaptureBindingSnapshot", consume)

        self.assertLess(consume, quarantine)
        self.assertLess(quarantine, capture_bindings)
        self.assertLess(reset_synthetic, capture_bindings)

    def test_owner_shutdown_discards_unapplied_recovery(self):
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")
        unregister = source.index("void InputFramePump::Unregister()")
        reset = source.index("InputRecoveryMailbox::GetSingleton().Reset()", unregister)
        process = source.index("InputFramePump::ProcessEvent", unregister)

        self.assertLess(reset, process)


if __name__ == "__main__":
    unittest.main()
