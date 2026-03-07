#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <memory>

struct IWICImagingFactory;

namespace dualpad::utils
{
    // Raw BGRA frame copied from the render back buffer.
    struct ScreenshotData
    {
        std::vector<uint8_t> pixels;
        uint32_t width;
        uint32_t height;
        uint32_t rowPitch;
        std::string filename;
    };

    // Captures frames on demand and saves them on a background thread.
    class ScreenshotManager
    {
    public:
        static ScreenshotManager& GetSingleton();

        void Start();
        void Stop();

        // Copies the current back buffer into the save queue.
        bool CaptureFrame();

        size_t GetQueueSize() const;

        bool IsReady() const { return _initialized && _running && _wicFactory != nullptr; }

        void SetMaxQueueSize(size_t maxSize) { _maxQueueSize = maxSize; }

    private:
        ScreenshotManager() = default;
        ~ScreenshotManager();

        void SaverThread();

        bool SaveToFile(const ScreenshotData& data);

        std::string GenerateFilename();

        std::string GetSavePath();

        std::jthread _thread;
        std::queue<ScreenshotData> _queue;
        mutable std::mutex _mutex;
        std::condition_variable _cv;
        std::atomic<bool> _running{ false };
        std::atomic<bool> _initialized{ false };

        std::chrono::steady_clock::time_point _lastCaptureTime;
        std::mutex _captureMutex;
        // Prevents repeated hotkey presses from flooding the save queue.
        static constexpr int MIN_CAPTURE_INTERVAL_MS = 200;

        size_t _maxQueueSize{ 10 };
        // Cached because creating the WIC factory on every capture is expensive.
        IWICImagingFactory* _wicFactory{ nullptr };
        std::mutex _wicMutex;
    };
}
