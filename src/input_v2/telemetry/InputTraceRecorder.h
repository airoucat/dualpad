#pragma once

#include "input/AuthoritativePollState.h"
#include "input/backend/ActionOutputContract.h"
#include "input/backend/KeyboardNativeBridge.h"
#include "input/glyph/GlyphResolutionCompat.h"
#include "input/injection/PadEventSnapshot.h"
#include "input/injection/RouteHealthContract.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>

namespace dualpad::input_v2::gameplay
{
    struct RuntimeDebugSnapshot;
}

namespace dualpad::input_v2::telemetry
{
    struct ReplayCompatibilitySurface
    {
        input::InputContext context{ input::InputContext::Gameplay };
        std::uint32_t contextEpoch{ 0 };
        bool isUsingGamepad{ false };
        bool gamepadControlsCursor{ false };
        bool gamepadDeviceEnabled{ false };
        std::string presentationOwner;
        std::string cursorOwner;
        std::string gameplayEngineOwner;
        std::string gameplayMenuEntryOwner;
    };

    class InputTraceRecorder
    {
    public:
        static InputTraceRecorder& GetSingleton();

        void ResetSession();
        void BeginReplaySession(const std::filesystem::path& root, std::string_view session);
        void EndReplaySession();
        void SetActiveSnapshotSequence(std::uint64_t sequence);
        void RecordDispatcherSubmit(
            const input::PadEventSnapshot& snapshot,
            std::size_t pendingBefore,
            std::size_t pendingAfter);
        void RecordDispatcherDrain(
            const input::DrainTelemetryContext& telemetry,
            std::size_t budget,
            std::size_t drained,
            std::size_t pendingBefore,
            std::size_t pendingAfter);
        void RecordProcessedSnapshot(
            const input::PadEventSnapshot& snapshot,
            const input::AuthoritativePollFrame& pollFrame,
            const ReplayCompatibilitySurface& presentationSurface);
        void RecordKeyboardCommand(
            input::backend::KeyboardBridgeCommandType type,
            std::uint8_t scancode,
            std::string_view actionId,
            input::backend::ActionOutputContract contract,
            input::InputContext context);
        void RecordGlyphResult(
            std::string_view actionId,
            std::string_view requestedContextName,
            const input::glyph::GlyphResolutionCompatResult& resolution);
        void RecordRuntimeDebugSnapshot(const gameplay::RuntimeDebugSnapshot& snapshot);

    private:
        InputTraceRecorder() = default;

        bool EnsureSessionLocked();
        void EnsureHeadersLocked();
        void EnsureOptionalHeaderLocked(std::string_view fileName, std::string_view header);
        void AppendLineLocked(std::string_view fileName, std::string_view line);
        std::filesystem::path ResolveSessionDirectoryLocked() const;

        std::mutex _mutex;
        std::filesystem::path _activeRoot;
        std::string _activeSession;
        bool _headersReady{ false };
        bool _replaySessionActive{ false };
        std::uint64_t _activeSnapshotSequence{ 0 };
        std::uint64_t _keyboardCommandSequence{ 0 };
        std::uint64_t _scheduleStepIndex{ 0 };
        std::uint64_t _keyboardCommandIndex{ 0 };
        std::uint64_t _glyphQueryId{ 0 };
    };
}
