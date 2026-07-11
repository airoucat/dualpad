import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]


class RuntimeUiThreadBoundaryTests(unittest.TestCase):
    def test_runtime_owner_never_captures_live_ui(self) -> None:
        source = (ROOT / "src/input_v2/context/ContextRefreshTick.cpp").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("observer.Capture()", source)
        self.assertNotIn("RE::UI", source)

    def test_menu_event_queues_ui_thread_capture(self) -> None:
        event_sink = (ROOT / "src/input/ContextEventSink.cpp").read_text(
            encoding="utf-8"
        )
        observer = (ROOT / "src/input_v2/menu/UiMenuObserver.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("QueueCaptureOnUiThread()", event_sink)
        self.assertIn("AddUITask", observer)
        self.assertIn("PublishCapturedSnapshot", observer)

    def test_prompt_attach_only_runs_from_ui_task(self) -> None:
        source = (ROOT / "src/input_v2/prompt/ScaleformPromptAdapter.cpp").read_text(
            encoding="utf-8"
        )
        on_open = source.split("void ScaleformPromptAdapter::OnMenuOpened", 1)[1].split(
            "void ScaleformPromptAdapter::OnMenuClosed", 1
        )[0]

        self.assertNotIn("\n        AttachToMenu(menu);", on_open)
        self.assertIn("AddUITask", on_open)

    def test_hud_action_only_reads_ui_from_ui_task(self) -> None:
        source = (ROOT / "src/input/ActionExecutor.cpp").read_text(encoding="utf-8")
        execute_plugin = source.split("bool ActionExecutor::ExecutePluginAction", 1)[
            1
        ]

        self.assertNotIn(
            "\n        auto* ui = RE::UI::GetSingleton();\n", execute_plugin
        )
        self.assertIn("taskInterface->AddUITask", execute_plugin)


if __name__ == "__main__":
    unittest.main()
