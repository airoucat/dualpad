import json
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
EVALUATOR = ROOT / "scripts" / "ci" / "evaluate_mixed_input_trace.py"
FIXTURES = ROOT / "tests" / "fixtures" / "mixed_input"


class MixedInputTraceEvaluatorTests(unittest.TestCase):
    def evaluate(self, fixture: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(EVALUATOR), str(FIXTURES / fixture)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

    def assert_violation(self, fixture: str, *contracts: str) -> None:
        result = self.evaluate(fixture)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        for contract in contracts:
            self.assertIn(contract, result.stdout + result.stderr)

    def test_good_fixture_passes(self) -> None:
        result = self.evaluate("good.jsonl")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        summary = json.loads(result.stdout)
        self.assertEqual(summary["status"], "PASS")
        self.assertEqual(summary["violations"], [])

    def test_neutral_hid_takeover_is_rejected(self) -> None:
        self.assert_violation("neutral_hid_takeover.jsonl", "B.1-7")

    def test_both_time_views_reject_double_writers(self) -> None:
        self.assert_violation(
            "double_writer.jsonl",
            "B.3-current-cycle-writer",
            "B.3-next-poll-writer",
        )

    def test_sprint_stuck_or_missing_final_release_is_rejected(self) -> None:
        self.assert_violation("stuck_sprint.jsonl", "B.3-sprint-held", "B.3-sprint-final-release")

    def test_stale_epoch_or_session_apply_is_rejected(self) -> None:
        self.assert_violation("stale_epoch_session.jsonl", "B.3-stale-epoch", "B.3-stale-session")

    def test_engine_scope_leak_is_rejected(self) -> None:
        self.assert_violation("engine_scope_leak.jsonl", "C.2-engine-original")

    def test_cursor_commit_without_exact_ack_is_rejected(self) -> None:
        self.assert_violation("cursor_commit_without_ack.jsonl", "B.3-cursor-ack")

    def test_cursor_owner_request_without_pending_plan_is_rejected(self) -> None:
        self.assert_violation(
            "cursor_request_without_pending.jsonl",
            "B.3-cursor-pending",
        )

    def test_poll_receipt_failures_are_rejected(self) -> None:
        self.assert_violation("poll_receipt_missing.jsonl", "B.3-poll-receipt")
        self.assert_violation(
            "poll_receipt_ambiguous_reused.jsonl",
            "B.3-poll-receipt-ambiguous",
            "B.3-poll-receipt-reused",
        )

    def test_adapter_failure_cannot_advance_sensitive_ledger(self) -> None:
        self.assert_violation("adapter_failure_ledger_advance.jsonl", "B.3-adapter-rollback")

    def test_synthetic_takeover_is_rejected(self) -> None:
        self.assert_violation("synthetic_takeover.jsonl", "B.1-7-synthetic")


if __name__ == "__main__":
    unittest.main()
