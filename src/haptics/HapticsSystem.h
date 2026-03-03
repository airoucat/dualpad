#pragma once
#include <atomic>

namespace dualpad::haptics
{
    // 触觉系统总控
    // 负责初始化、启动、停止所有触觉子系统
    class HapticsSystem
    {
    public:
        static HapticsSystem& GetSingleton();

        // 初始化所有子系统
        bool Initialize();

        // 启动触觉系统（启动线程）
        bool Start();

        // 停止触觉系统
        void Stop();

        // 关闭并清理资源
        void Shutdown();

        // 查询状态
        bool IsRunning() const { return _running.load(std::memory_order_acquire); }
        bool IsInitialized() const { return _initialized; }

        // 打印统计信息（调试用）
        void PrintStats();

    private:
        HapticsSystem() = default;

        bool _initialized{ false };
        std::atomic<bool> _running{ false };

        // 初始化步骤
        bool InitializeConfig();
        bool InitializeQueues();
        bool InitializeManagers();
        bool InitializeThreads();

        // 关闭步骤
        void StopThreads();
        void ShutdownManagers();
    };
}