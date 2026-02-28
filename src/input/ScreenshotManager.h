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
    // 截图数据结构
    struct ScreenshotData
    {
        std::vector<uint8_t> pixels;  // 像素数据
        uint32_t width;
        uint32_t height;
        uint32_t rowPitch;
        std::string filename;
    };

    // 截图管理器（单例）
    class ScreenshotManager
    {
    public:
        static ScreenshotManager& GetSingleton();

        // 启动/停止后台保存线程
        void Start();
        void Stop();

        // 截取当前帧（快速，不阻塞）
        bool CaptureFrame();

        // 获取待保存的截图数量
        size_t GetQueueSize() const;

        // 检查是否已初始化
        bool IsReady() const { return _initialized && _running && _wicFactory != nullptr; }

        // 设置最大队列大小
        void SetMaxQueueSize(size_t maxSize) { _maxQueueSize = maxSize; }

    private:
        ScreenshotManager() = default;
        ~ScreenshotManager();

        // 后台保存线程
        void SaverThread();

        // 保存单张截图到文件
        bool SaveToFile(const ScreenshotData& data);

        // 生成文件名
        std::string GenerateFilename();

        // 获取保存路径
        std::string GetSavePath();

        std::jthread _thread;
        std::queue<ScreenshotData> _queue;
        mutable std::mutex _mutex;
        std::condition_variable _cv;
        std::atomic<bool> _running{ false };
        std::atomic<bool> _initialized{ false };  // 添加这行：初始化完成标志

        std::chrono::steady_clock::time_point _lastCaptureTime;  // 上次截图时间
        std::mutex _captureMutex;  // 截图互斥锁
        static constexpr int MIN_CAPTURE_INTERVAL_MS = 200;  // 最小截图间隔 200ms

        size_t _maxQueueSize{ 10 };  // 默认最多缓存 10 张截图

        // 缓存 WIC Factory（避免重复创建）
        IWICImagingFactory* _wicFactory{ nullptr };
        std::mutex _wicMutex;
    };
}