#pragma once
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct IWICImagingFactory;

namespace dualpad::input::custom
{
    enum class ScreenshotRequestStatus
    {
        Queued,
        Debounced,
        NotReady,
        UnsupportedFormat,
        CaptureFailed
    };

    struct ScreenshotRequestResult
    {
        ScreenshotRequestStatus status{ ScreenshotRequestStatus::NotReady };
        std::string filename;
        std::size_t queueDepth{ 0 };

        [[nodiscard]] bool IsQueued() const
        {
            return status == ScreenshotRequestStatus::Queued;
        }

        [[nodiscard]] bool IsHandled() const
        {
            return status == ScreenshotRequestStatus::Queued ||
                status == ScreenshotRequestStatus::Debounced;
        }
    };

    class ScreenshotActionService
    {
    public:
        static ScreenshotActionService& GetSingleton();

        void Start();
        void Stop();

        [[nodiscard]] ScreenshotRequestResult RequestCapture();
        [[nodiscard]] bool IsReady() const;

    private:
        struct ScreenshotData
        {
            std::vector<std::uint8_t> pixels;
            std::uint32_t width{ 0 };
            std::uint32_t height{ 0 };
            std::uint32_t rowPitch{ 0 };
            std::string filename;
        };

        enum class StartupState
        {
            Stopped,
            Starting,
            Stopping,
            Ready,
            Failed
        };

        ScreenshotActionService() = default;
        ~ScreenshotActionService();

        ScreenshotActionService(const ScreenshotActionService&) = delete;
        ScreenshotActionService(ScreenshotActionService&&) = delete;
        ScreenshotActionService& operator=(const ScreenshotActionService&) = delete;
        ScreenshotActionService& operator=(ScreenshotActionService&&) = delete;

        void SaverThread();
        [[nodiscard]] bool SaveToFile(
            IWICImagingFactory& wicFactory,
            const std::wstring& saveDirectory,
            const ScreenshotData& data) const;
        [[nodiscard]] std::wstring ResolveSaveDirectory() const;
        [[nodiscard]] std::string GenerateFilename() const;

        static constexpr int MIN_CAPTURE_INTERVAL_MS = 200;
        static constexpr std::size_t DEFAULT_MAX_QUEUE_SIZE = 10;

        mutable std::mutex _stateMutex;
        std::condition_variable _startupCv;
        StartupState _startupState{ StartupState::Stopped };
        std::wstring _saveDirectory;

        std::deque<ScreenshotData> _queue;
        mutable std::mutex _queueMutex;
        std::condition_variable _queueCv;
        bool _stopRequested{ false };

        mutable std::mutex _captureMutex;
        std::chrono::steady_clock::time_point _lastCaptureTime{};

        std::jthread _thread;
        std::size_t _maxQueueSize{ DEFAULT_MAX_QUEUE_SIZE };
    };
}
