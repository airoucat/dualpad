import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputNativeKbmSemanticsWiringTests(unittest.TestCase):
    def test_poll_hook_releases_suppression_before_native_controlmap_mapping(self) -> None:
        source = (ROOT / "src/input/injection/UpstreamGamepadHook.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("NativeKbmSemanticPolicy.h", source)
        self.assertIn("ApplyNativeKbmSemanticPolicy(", source)
        self.assertIn("GetRuntimeData().ignoreKeyboardMouse", source)
        self.assertIn("outputFrame->remapMode", source)

        acquire = source.index("AcquireForPoll()")
        policy = source.index("ApplyNativeKbmSemanticPolicy(")
        serialize = source.index("FillSyntheticXInputState(")
        self.assertLess(acquire, policy)
        self.assertLess(policy, serialize)

    def test_fix_does_not_rewrite_native_input_events(self) -> None:
        source = (ROOT / "src/input/injection/UpstreamGamepadHook.cpp").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("SetUserEvent(", source)
        self.assertNotIn("AddButtonEvent(", source)


if __name__ == "__main__":
    unittest.main()
