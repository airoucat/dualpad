import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class MixedInputWp5ShadowWiringTests(unittest.TestCase):
    def test_verified_hook_publishes_receipt_after_serialize(self):
        source = (ROOT / "src/input/injection/UpstreamGamepadHook.cpp").read_text(encoding="utf-8")
        serialize = source.index("FillSyntheticXInputState")
        publish = source.index("PublishAfterSuccessfulSerialize")
        self.assertGreater(publish, serialize)
        self.assertIn("result == ERROR_SUCCESS", source[serialize:publish + 300])

    def test_pump_consumes_exact_receipt_without_reacquiring_latest_poll(self):
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")
        self.assertIn("ConsumeForThread", source)
        self.assertIn("BuildCurrentCycleGatePlan", source)
        self.assertIn("AuditEventListShadow", source)
        self.assertIn("PrepareCallbackAudit", source)
        self.assertIn("CommitCallbackAudit", source)
        self.assertNotIn("AcquireForPoll", source)
        self.assertLess(source.index("ConsumeForThread"), source.index("DrainOnOwnerTick"))
        self.assertLess(source.index("PrepareCallbackAudit"), source.index("AuditEventListShadow"))
        self.assertLess(source.index("AuditEventListShadow"), source.index("CommitCallbackAudit"))

    def test_event_mutation_remains_compile_time_disabled(self):
        header = (ROOT / "src/input/injection/SkyrimCurrentCycleEventAdapter.h").read_text(encoding="utf-8")
        self.assertIn("ProductionMutationEnabled() noexcept { return false; }", header)

    def test_pump_attaches_receipt_and_current_boundary_to_shadow_evidence(self):
        source = (ROOT / "src/input/InputFramePump.cpp").read_text(encoding="utf-8")
        prepare = source[source.index("PrepareCallbackAudit"):source.index("AuditEventListShadow")]
        for token in [
            "CurrentCycleCallbackEvidence",
            ".ownerTickToken = frameToken",
            ".monotonicUs = ownerNowUs",
            ".currentInputStateEpoch = kbmReceipt.inputStateEpoch",
            ".currentGamepadSessionId = kbmReceipt.gamepadSessionId",
            ".receiptFailure = consumedReceipt.failure",
            ".receipt = consumedReceipt.receipt",
        ]:
            self.assertIn(token, prepare)

    def test_runtime_attaches_sprint_decision_to_shadow_evidence(self):
        source = (ROOT / "src/input_v2/gameplay/DualPadRuntime.cpp").read_text(encoding="utf-8")
        start = source.index("MixedInputEvidenceRecord{")
        end = source.index("});", start)
        self.assertIn(".sprintDecision = projection.sprintDecision", source[start:end])


if __name__ == "__main__":
    unittest.main()
