#pragma once

#include "haptics/HapticsTypes.h"
#include "haptics/SemanticCacheTypes.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dualpad::haptics
{
    class FormSemanticCache
    {
    public:
        struct Value
        {
            SemanticGroup group{ SemanticGroup::Unknown };
            float confidence{ 0.5f };
            float baseWeight{ 0.5f };
            float weight{ 0.5f }; // 兼容旧代码（=baseWeight）
            std::uint16_t texturePresetId{ 0 };
            SemanticFlags flags{ SemanticFlags::None };
        };

        struct Snapshot
        {
            std::unordered_map<std::uint32_t, SemanticMeta> table;
            std::uint64_t fingerprintHash{ 0 };
            std::uint32_t rulesVersion{ 0 };
        };

        struct Stats
        {
            std::uint64_t queries{ 0 };
            std::uint64_t hits{ 0 };
            std::uint64_t fallbacks{ 0 };
            std::uint64_t unknownFallbacks{ 0 };
            std::size_t snapshotSize{ 0 };
        };

        static FormSemanticCache& GetSingleton();

        // 启动期调用
        void WarmupDefaults();

        // 安装只读快照（atomic swap）
        void InstallSnapshot(std::shared_ptr<const Snapshot> snapshot);

        // 用 records 直接构建并安装
        void InstallFromRecords(
            const std::vector<FormSemanticRecord>& records,
            std::uint64_t fingerprintHash,
            std::uint32_t rulesVersion);

        // 兼容：少量手动覆盖（copy-on-write，避免运行期高频调用）
        void Set(std::uint32_t formId, const SemanticMeta& meta);

        Value Resolve(std::uint32_t formId, EventType fallbackType) const;

        std::shared_ptr<const Snapshot> GetSnapshot() const;
        std::size_t Size() const;

        Stats GetStats() const;
        void ResetStats();

    private:
        FormSemanticCache() = default;
        SemanticGroup FallbackFromEvent(EventType type) const;

        // 注意：用 atomic_load/atomic_store 操作 shared_ptr
        std::shared_ptr<const Snapshot> _snapshot;

        mutable std::atomic<std::uint64_t> _queries{ 0 };
        mutable std::atomic<std::uint64_t> _hits{ 0 };
        mutable std::atomic<std::uint64_t> _fallbacks{ 0 };
        mutable std::atomic<std::uint64_t> _unknownFallbacks{ 0 };
    };
}