import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / "scripts" / "ci" / "check_mixed_input_ida_static_evidence.py"
EVIDENCE = ROOT / ".dualpad-builder" / "mixed_input_ida_static_evidence.json"


class MixedInputIdaStaticEvidenceTests(unittest.TestCase):
    def run_checker(self, evidence: Path = EVIDENCE) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), "--evidence", str(evidence)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=False,
        )

    def write_mutated(self, mutation) -> subprocess.CompletedProcess[str]:
        evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))
        mutation(evidence)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ida-static.json"
            path.write_text(json.dumps(evidence), encoding="utf-8")
            return self.run_checker(path)

    def test_matching_skyrim_1597_static_evidence_passes(self) -> None:
        result = self.run_checker()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("directCodeXrefs=26", result.stdout)
        self.assertIn("dynamicGatesRemain=NO-GO", result.stdout)

    def test_wrong_binary_hash_is_rejected(self) -> None:
        result = self.write_mutated(lambda evidence: evidence.update(inputSha256="00" * 32))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256", result.stdout + result.stderr)

    def test_xref_count_or_target_drift_is_rejected(self) -> None:
        count_result = self.write_mutated(
            lambda evidence: evidence.update(directCodeXrefCount=25)
        )
        self.assertNotEqual(count_result.returncode, 0)
        self.assertIn("26", count_result.stdout + count_result.stderr)

        def change_target(evidence: dict) -> None:
            evidence["directCodeXrefs"][0]["resolvedTargetVa"] = "0x140000000"

        target_result = self.write_mutated(change_target)
        self.assertNotEqual(target_result.returncode, 0)
        self.assertIn("resolved target", target_result.stdout + target_result.stderr)

        def substitute_callsite(evidence: dict) -> None:
            evidence["directCodeXrefs"][0]["callsiteVa"] = "0x140705077"

        callsite_result = self.write_mutated(substitute_callsite)
        self.assertNotEqual(callsite_result.returncode, 0)
        self.assertIn("xref inventory", callsite_result.stdout + callsite_result.stderr)

    def test_required_signature_site_drift_is_rejected(self) -> None:
        def remove_cursor_site(evidence: dict) -> None:
            evidence["signatureSites"] = [
                site
                for site in evidence["signatureSites"]
                if site["va"].lower() != "0x140ed2f90"
            ]

        result = self.write_mutated(remove_cursor_site)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("0x140ed2f90", result.stdout + result.stderr)

    def test_reviewed_static_classification_can_reduce_unknown_count(self) -> None:
        def classify_menu_caller(evidence: dict) -> None:
            evidence["directCodeXrefs"][-1]["classification"] = "MenuSetPlatform"
            evidence["directCodeXrefs"][-1]["causality"] = "OriginalOnly"
            evidence["directCodeXrefs"][-1]["classificationEvidence"] = (
                "approved plan I.6 identifies 0x140ECD970 as menu SetPlatform"
            )
            evidence["releaseRelevantUnknownCallers"] = 25

        result = self.write_mutated(classify_menu_caller)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_classification_without_evidence_is_rejected(self) -> None:
        def classify_without_evidence(evidence: dict) -> None:
            evidence["directCodeXrefs"][-1]["classification"] = "MenuSetPlatform"
            evidence["releaseRelevantUnknownCallers"] = 25

        result = self.write_mutated(classify_without_evidence)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("classification evidence", result.stdout + result.stderr)

    def test_query_entry_semantics_and_private_paths_are_locked(self) -> None:
        slot_result = self.write_mutated(
            lambda evidence: evidence["queryEntry"].update(virtualSlotIndex=8)
        )
        self.assertNotEqual(slot_result.returncode, 0)
        self.assertIn("slot", slot_result.stdout + slot_result.stderr)

        path_result = self.write_mutated(
            lambda evidence: evidence.update(notes="captured from C:\\private\\host.i64")
        )
        self.assertNotEqual(path_result.returncode, 0)
        self.assertIn("machine-private", path_result.stdout + path_result.stderr)

    def test_handler_col_vftable_identity_is_required(self) -> None:
        def remove_handler_identity(evidence: dict) -> None:
            evidence.pop("handlerTypeIdentity", None)

        missing_result = self.write_mutated(remove_handler_identity)
        self.assertNotEqual(missing_result.returncode, 0)
        self.assertIn("handler", missing_result.stdout + missing_result.stderr)

        def move_vftable_to_col(evidence: dict) -> None:
            evidence["handlerTypeIdentity"]["vftableVa"] = "0x14175e848"

        drift_result = self.write_mutated(move_vftable_to_col)
        self.assertNotEqual(drift_result.returncode, 0)
        self.assertIn("vftable", drift_result.stdout + drift_result.stderr)

    def test_extended_handler_identity_requires_schema_v2(self) -> None:
        result = self.write_mutated(
            lambda evidence: evidence.update(schemaVersion=1)
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("schemaVersion", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
