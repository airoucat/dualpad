#pragma once
#include <atomic>

namespace dualpad::haptics
{
    class HapticsSystem
    {
    public:
        static HapticsSystem& GetSingleton();

        bool Initialize();
        bool Start();
        void Stop();
        void Shutdown();

        bool IsRunning() const { return _running.load(std::memory_order_acquire); }
        bool IsInitialized() const { return _initialized; }

        void PrintStats();

    private:
        HapticsSystem() = default;

        bool _initialized{ false };
        std::atomic<bool> _running{ false };

        // 自定义链路是否激活（tap + mixer）
        std::atomic<bool> _customPipelineActive{ false };

        // 核心音频链是否初始化（VoiceManager 等）
        std::atomic<bool> _corePipelineInitialized{ false };

        bool InitializeConfig();
        bool InitializeCorePipeline();   // 纯音频核心：VoiceManager
        bool InitializeThreads();

        void StopThreads();
        void ShutdownCorePipeline();
        void PrintSessionSummary();
    };
}