import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp3BoundaryTests(unittest.TestCase):
    def test_runtime_dispatcher_uses_full_causal_capture(self) -> None:
        contents = (ROOT / "src/input/injection/PadEventSnapshotDispatcher.cpp").read_text(encoding="utf-8")
        self.assertGreaterEqual(contents.count("RuntimeFrameAssembler().Assemble(capture)"), 2)
        self.assertNotIn(
            "RuntimeFrameAssembler().Assemble(\n            capture.events,",
            contents,
        )
        self.assertGreaterEqual(contents.count("ConsumeGlobalResetRequest()"), 2)
        self.assertGreaterEqual(contents.count("PublishGlobalReset("), 2)

    def test_hid_reader_tags_every_report_with_acknowledged_session(self) -> None:
        contents = (ROOT / "src/input/HidReader.cpp").read_text(encoding="utf-8")
        self.assertIn("producerGamepadSessionId", contents)
        self.assertIn("receipt.gamepadSessionId", contents)
        self.assertIn("disconnectReceipt.gamepadSessionId", contents)
        self.assertIn("receipt.staleGamepadSession", contents)


if __name__ == "__main__":
    unittest.main()
