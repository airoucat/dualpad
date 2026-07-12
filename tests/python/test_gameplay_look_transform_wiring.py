from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class GameplayLookTransformWiringTests(unittest.TestCase):
    def read(self, relative: str) -> str:
        path = ROOT / relative
        self.assertTrue(path.is_file(), f"missing production wiring: {relative}")
        return path.read_text(encoding="utf-8")

    def test_source_specific_handlers_scope_only_the_native_look_transform(self) -> None:
        hook = self.read("src/input/injection/GameplayLookTransformHook.cpp")

        for token in (
            "RE::VTABLE_LookHandler[0]",
            "0x7091F0",
            "0x7091C0",
            "0x70711D",
            "0x705B12",
            "0x705AE0",
            "0xC15240",
            "GameplayLookInputSource::Mouse",
            "GameplayLookInputSource::Gamepad",
            "ConsumeFor",
            "EnterEventLocalLookOverride",
            "ExecutePatchTransaction",
        ):
            self.assertIn(token, hook)

        self.assertNotIn("powf(", hook)
        self.assertNotIn("deadzone", hook.lower())

    def test_plugin_installs_the_independent_fail_closed_hook(self) -> None:
        main = self.read("src/main.cpp")
        header = self.read("src/input/injection/GameplayLookTransformHook.h")

        self.assertIn('#include "input/injection/GameplayLookTransformHook.h"', main)
        self.assertIn("GameplayLookTransformHook::GetSingleton().Install()", main)
        self.assertIn("bool Install();", header)
        self.assertNotIn("InstallI2DiagnosticCandidate", main)
        self.assertNotIn("InstallI2DiagnosticCandidate", header)


if __name__ == "__main__":
    unittest.main()
