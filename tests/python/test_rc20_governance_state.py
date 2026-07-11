import os
import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class Rc20GovernanceStateTests(unittest.TestCase):
    def run_gate(self, relative: str) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"
        return subprocess.run(
            [sys.executable, str(ROOT / relative)],
            cwd=ROOT,
            env=env,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

    def test_reviewed_docs_accept_completed_conditional_hotfix(self) -> None:
        result = self.run_gate("scripts/ci/check_reviewed_docs_consistency.py")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rc_closeout_accepts_completed_conditional_hotfix(self) -> None:
        result = self.run_gate("scripts/ci/check_rc_readiness_closeout.py")

        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
