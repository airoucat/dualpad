import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp1WiringTests(unittest.TestCase):
    def test_live_hid_uses_classified_batch_and_separate_disconnect(self) -> None:
        contents = (ROOT / "src" / "input" / "HidReader.cpp").read_text(encoding="utf-8")

        for required in (
            "GamepadActivityClassifier",
            ".Classify(",
            "PublishGamepadBatch(",
            "PublishGamepadDisconnect(",
            "NotifyIngressPublished(",
        ):
            self.assertIn(required, contents)

        for legacy_path in (
            "CollectGamepadSourceEvidence",
            "LiveInputFactProducer",
            "SubmitSnapshot(",
        ):
            self.assertNotIn(legacy_path, contents)

    def test_classifier_remains_context_neutral(self) -> None:
        contents = (
            ROOT / "src" / "input_v2" / "ingress" / "GamepadActivityClassifier.cpp"
        ).read_text(encoding="utf-8")

        for forbidden in ("InputContext", "GameplayChannel", "ActionId", "context::"):
            self.assertNotIn(forbidden, contents)

    def test_runtime_and_focused_targets_share_classifier_source(self) -> None:
        contents = (ROOT / "xmake.lua").read_text(encoding="utf-8")

        self.assertIn(
            '"src/input_v2/ingress/GamepadActivityClassifier.cpp"',
            contents,
        )
        self.assertIn('add_files("src/**.cpp")', contents)
        self.assertIn("add_files(table.unpack(ph7_ingress_files))", contents)


if __name__ == "__main__":
    unittest.main()
