import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts" / "ci" / "check_mixed_input_dynamic_evidence.py"
MANIFEST = ROOT / ".dualpad-builder" / "mixed_input_evidence.json"
DOCUMENT = ROOT / "docs" / "research" / "skyrim_mixed_input_dynamic_evidence_zh.md"


class MixedInputDynamicEvidenceTests(unittest.TestCase):
    def run_checker(self, manifest: Path = MANIFEST) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [
                sys.executable,
                str(CHECKER),
                "--manifest",
                str(manifest),
                "--document",
                str(DOCUMENT),
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

    def test_recorded_no_go_gates_are_consistent(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("releaseStatus=NO-GO", result.stdout)

    def test_unproven_capability_cannot_be_enabled(self) -> None:
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        manifest["dynamicEvidence"]["gates"]["I-P"]["capabilityEnabled"] = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            path.write_text(json.dumps(manifest), encoding="utf-8")
            result = self.run_checker(path)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("I-P", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
