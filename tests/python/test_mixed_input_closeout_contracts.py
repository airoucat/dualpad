import argparse
import json
import pathlib
import re
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
AUDIT_SNAPSHOT = "93184697c0f021a5979c9a71113bcb2a96308231"
EVIDENCE_MANIFEST = ROOT / ".dualpad-builder" / "mixed_input_evidence.json"


def git(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=ROOT,
        capture_output=True,
        text=True,
        encoding="utf-8",
        check=False,
    )


def native_button_has_sprint_source_mask_coverage(contents: str) -> bool:
    normalized = re.sub(r"[^a-z0-9]+", " ", contents.lower())
    return all(token in normalized for token in ("sprint", "contributor", "source mask"))


class MixedInputCloseoutContractTests(unittest.TestCase):
    phase = "preflight"

    def load_manifest(self) -> dict:
        self.assertTrue(
            EVIDENCE_MANIFEST.is_file(),
            "mixed-input evidence manifest must freeze the implementation base",
        )
        return json.loads(EVIDENCE_MANIFEST.read_text(encoding="utf-8"))

    def test_audit_snapshot_and_native_button_fixture_exist(self) -> None:
        audit = git("cat-file", "-e", f"{AUDIT_SNAPSHOT}^{{commit}}")
        self.assertEqual(audit.returncode, 0, audit.stderr)

        fixture = git(
            "cat-file",
            "-e",
            f"{AUDIT_SNAPSHOT}:tests/NativeButtonCommitTests.cpp",
        )
        self.assertEqual(fixture.returncode, 0, fixture.stderr)
        self.assertTrue((ROOT / "tests" / "NativeButtonCommitTests.cpp").is_file())

    def test_evidence_freezes_base_without_zero_diff_closeout_policy(self) -> None:
        manifest = self.load_manifest()
        base = manifest["implementationBaseCommit"]

        self.assertEqual(manifest["auditSnapshot"], AUDIT_SNAPSHOT)
        self.assertRegex(base, r"^[0-9a-f]{40}$")
        self.assertIn("codeDiffFromAuditSnapshot", manifest)
        self.assertFalse(manifest["closeoutPolicy"]["requireZeroDiffFromImplementationBase"])

        head = git("rev-parse", "HEAD")
        self.assertEqual(head.returncode, 0, head.stderr)
        if self.phase == "preflight":
            self.assertEqual(
                head.stdout.strip(),
                base,
                "preflight must run at the frozen implementation base",
            )
        else:
            ancestor = git("merge-base", "--is-ancestor", base, "HEAD")
            self.assertEqual(
                ancestor.returncode,
                0,
                "closeout HEAD must descend from implementationBaseCommit",
            )

    def test_phase8_builds_and_runs_gameplay_projection_tests(self) -> None:
        contents = (ROOT / "scripts" / "ci" / "run_phase8_ci.ps1").read_text(
            encoding="utf-8"
        )
        for verb in ("build", "run"):
            command = (
                f'Invoke-Step xmake @("{verb}", "-y", '
                '"DualPadGameplayProjectionTests")'
            )
            self.assertIn(
                command,
                contents,
                f"canonical CI must include xmake {verb} -y DualPadGameplayProjectionTests",
            )

    def test_native_button_audit_reports_deferred_sprint_coverage(self) -> None:
        manifest = self.load_manifest()
        contents = (ROOT / "tests" / "NativeButtonCommitTests.cpp").read_text(
            encoding="utf-8"
        )
        actual_coverage = native_button_has_sprint_source_mask_coverage(contents)
        recorded = manifest["nativeButtonAudit"]

        self.assertTrue(recorded["targetExists"])
        self.assertTrue(recorded["fileExists"])
        self.assertEqual(recorded["sprintContributorSourceMaskCoverage"], actual_coverage)
        if self.phase == "closeout":
            self.assertTrue(
                actual_coverage,
                "closeout requires Sprint contributor/source-mask coverage from its gated WP",
            )

    def test_source_list_contract_keeps_runtime_and_focused_targets_in_sync(self) -> None:
        manifest = self.load_manifest()
        xmake = (ROOT / "xmake.lua").read_text(encoding="utf-8")

        self.assertIn('add_files("src/**.cpp")', xmake)
        self.assertRegex(
            xmake,
            r'(?s)target\("DualPadIngressTests"\).*?add_files\(table\.unpack\(ph7_ingress_files\)\)',
            "Ingress focused target must consume the shared ingress source list",
        )
        for source in manifest["sourceListParity"]["ingressScaffoldSources"]:
            self.assertIn(f'"{source}"', xmake)


def parse_args() -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--phase", choices=("preflight", "closeout"), required=True)
    return parser.parse_known_args()


if __name__ == "__main__":
    args, unittest_args = parse_args()
    MixedInputCloseoutContractTests.phase = args.phase
    unittest.main(argv=[sys.argv[0], *unittest_args])
