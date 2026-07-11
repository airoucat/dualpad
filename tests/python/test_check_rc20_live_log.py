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
            "[08:00:01.002] [200] [I] [DualPad][MenuRefreshTrace] event=done target=Main Menu refreshed=1",
            "[08:00:11.000] [200] [I] [DualPad][RuntimeOwner] event=tick generation=600 frameToken=600 threadHash=11",
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
        log = "\n".join(base_log().splitlines()[:-1])
        log += "\n[08:00:06.000] [300] [W] [DualPad][RuntimeOwner] event=rebound handoff=1 previousThreadHash=11 currentThreadHash=22 frameToken=360 generation=359"

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
        log += "\n[08:00:12.010] [200] [I] [DualPad][MenuRefreshTrace] event=done target=Main Menu refreshed=2"

        result = evaluate_live_log(log, expected_commit="71c57b30ae0a")

        self.assertEqual(result.status, LiveLogStatus.FAIL)
        self.assertIn("overflow", " ".join(result.reasons))
        self.assertIn("refreshed=2", " ".join(result.reasons))


if __name__ == "__main__":
    unittest.main()
