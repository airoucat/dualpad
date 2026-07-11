import unittest

from scripts.dev.check_rc20_live_log import LiveLogStatus, evaluate_live_log


def base_log(commit: str = "71c57b30ae0a") -> str:
    return "\n".join(
        [
            f"[08:00:00.000] [100] [I] [DualPad][Build] commit={commit} runtime=1-5-97-0",
            "[08:00:00.010] [100] [I] [DualPad][RuntimeConfig] pollDiagnostics=false nativeFavorites=false",
            "[08:00:00.020] [100] [I] [DualPad][UpstreamGamepad] Installed official Poll XInput call-site hook poll=0x1 callSite=0x2 originalTarget=0x3",
            "[08:00:01.000] [200] [I] [DualPad][RuntimeOwner] event=bound threadHash=11 frameToken=1",
            "[08:00:01.001] [200] [I] [DualPad][RuntimeOwner] event=tick generation=1 frameToken=1 threadHash=11",
            "[08:00:01.001] [200] [I] [DualPad][UiSnapshot] event=captured eventSeq=1 completeness=0 nodes=1 published=true retry=false",
            "[08:00:01.002] [200] [I] [DualPad][MenuRefreshTrace] event=done target=Main Menu refreshed=1 notified=0",
            "[08:00:11.000] [200] [I] [DualPad][RuntimeOwner] event=tick generation=600 frameToken=600 threadHash=11",
            "[08:00:12.000] [200] [I] [DualPad][CustomAction][Screenshot] Screenshot service stopped",
        ]
    )


class CheckRc20LiveLogTests(unittest.TestCase):
    def test_safe_smoke_without_handoff_passes(self) -> None:
        result = evaluate_live_log(base_log(), expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.PASS)
        self.assertEqual(result.latest_generation, 600)

    def test_serialized_handoff_requires_later_generation_on_new_thread(self) -> None:
        log = base_log() + "\n" + "\n".join(
            [
                "[08:00:06.000] [300] [W] [DualPad][RuntimeOwner] event=rebound handoff=1 previousThreadHash=11 currentThreadHash=22 frameToken=360 generation=359",
                "[08:00:10.000] [300] [I] [DualPad][RuntimeOwner] event=tick generation=600 frameToken=600 threadHash=22",
            ]
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.PASS)
        self.assertEqual(result.handoff_count, 1)

    def test_handoff_is_itself_an_accepted_serialized_tick(self) -> None:
        log = base_log()
        log += "\n[08:00:11.500] [300] [W] [DualPad][RuntimeOwner] event=rebound handoff=1 previousThreadHash=11 currentThreadHash=22 frameToken=601 generation=600"

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.PASS)
        self.assertEqual(result.handoff_count, 1)

    def test_thread_drift_is_a_failure(self) -> None:
        log = base_log() + "\n[08:00:06.000] [300] [C] [DualPad][RuntimeOwner] event=degraded failure=thread_drift frameToken=360"

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.FAIL)
        self.assertIn("thread_drift", " ".join(result.reasons))

    def test_wrong_build_or_enabled_native_favorites_fails_safe_smoke(self) -> None:
        log = base_log(commit="deadbeef0000").replace("nativeFavorites=false", "nativeFavorites=true")

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.FAIL)
        self.assertIn("build commit", " ".join(result.reasons))
        self.assertIn("native Favorites", " ".join(result.reasons))

    def test_overflow_or_multi_target_refresh_fails(self) -> None:
        log = base_log()
        log += "\n[08:00:12.000] [200] [W] transition=queue_overflow reasons=QueueOverflow"
        log += "\n[08:00:12.010] [200] [I] [DualPad][MenuRefreshTrace] event=done target=Main Menu refreshed=2 notified=0"

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.FAIL)
        self.assertIn("overflow", " ".join(result.reasons))
        self.assertIn("refreshed=2", " ".join(result.reasons))

    def test_missing_ui_task_capture_is_incomplete(self) -> None:
        log = "\n".join(
            line for line in base_log().splitlines() if "[UiSnapshot]" not in line
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("UI-task snapshot", " ".join(result.reasons))

    def test_initial_ui_capture_without_menu_event_is_incomplete(self) -> None:
        log = base_log().replace("eventSeq=1", "eventSeq=0")

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("menu-event UI-task snapshot", " ".join(result.reasons))

    def test_missing_target_refresh_is_incomplete(self) -> None:
        log = "\n".join(
            line
            for line in base_log().splitlines()
            if "[MenuRefreshTrace]" not in line
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("menu refresh", " ".join(result.reasons))

    def test_zero_target_refresh_is_incomplete(self) -> None:
        log = base_log().replace("refreshed=1", "refreshed=0")

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("menu refresh", " ".join(result.reasons))

    def test_ui_capture_queue_failure_fails(self) -> None:
        log = base_log()
        log += (
            "\n[08:00:02.000] [200] [W] [DualPad][UiSnapshot] "
            "event=queue_failed reason=no_skse_task_interface"
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.FAIL)
        self.assertIn("UI-task queue", " ".join(result.reasons))

    def test_short_smoke_is_incomplete(self) -> None:
        log = "\n".join(
            line for line in base_log().splitlines() if "generation=600" not in line
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("600", " ".join(result.reasons))

    def test_missing_clean_shutdown_is_incomplete(self) -> None:
        log = "\n".join(
            line
            for line in base_log().splitlines()
            if "Screenshot service stopped" not in line
        )

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.INCOMPLETE)
        self.assertIn("clean shutdown", " ".join(result.reasons))


if __name__ == "__main__":
    unittest.main()
