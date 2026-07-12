import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp8WiringTests(unittest.TestCase):
    def test_frame_assembler_keeps_ordered_activity_callback_local(self):
        source = (ROOT / "src/input_v2/ingress/FrameAssembler.cpp").read_text(encoding="utf-8")
        self.assertIn("facts.sourceActivities.clear()", source)
        self.assertIn("_window.facts.sourceActivities.push_back(event.sourceActivity)", source)

    def test_runtime_projects_one_atomic_presentation_snapshot(self):
        source = (ROOT / "src/input_v2/gameplay/DualPadRuntime.cpp").read_text(encoding="utf-8")
        self.assertIn(".ProjectOrdered(", source)
        self.assertIn("frame.facts.sourceActivities", source)
        self.assertIn("CursorHandoffAckMailbox::GetSingleton()", source)

    def test_runtime_records_cursor_plan_ack_and_original_engine_shadow(self):
        source = (ROOT / "src/input_v2/gameplay/DualPadRuntime.cpp").read_text(encoding="utf-8")
        start = source.index("void DualPadRuntime::PublishStablePresentationSurface")
        end = source.index("void DualPadRuntime::PublishRuntimeDebugSnapshot", start)
        body = source[start:end]
        for token in [
            "presentationBefore",
            "pendingCursorPlanBefore",
            "RecordPresentation",
            "cursorAck",
            "engineSnapshotCurrent",
            "GetEngineModeShadow",
        ]:
            self.assertIn(token, body)

    def test_pre_output_handoff_only_records_menu_entry_intent(self):
        source = (ROOT / "src/input_v2/presentation/SkyrimCompatibilitySurface.cpp").read_text(
            encoding="utf-8"
        )
        start = source.index("void SkyrimCompatibilitySurface::CommitPreOutputGameplayPresentationHandoff")
        end = source.index("void SkyrimCompatibilitySurface::EnableRollback", start)
        body = source[start:end]
        self.assertIn("gameplayMenuEntryIntentOwner", body)
        self.assertNotIn("_committed.owner =", body)
        self.assertNotIn("_committed.navigationOwner =", body)
        self.assertNotIn("_committed.cursorOwner =", body)


if __name__ == "__main__":
    unittest.main()
