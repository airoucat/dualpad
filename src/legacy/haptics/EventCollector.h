#pragma once
#include <RE/Skyrim.h>

namespace dualpad::haptics
{
    // SKSE 事件采集器
    // 监听游戏事件并转换为触觉事件
    class EventCollector :
        public RE::BSTEventSink<RE::BSAnimationGraphEvent>,
        public RE::BSTEventSink<RE::TESHitEvent>
    {
    public:
        static EventCollector& GetSingleton();

        // 注册事件监听
        void Register();

        // 取消注册
        void Unregister();

        // BSAnimationGraphEvent（脚步、挥刀等）
        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;

        // TESHitEvent（受击）
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESHitEvent* event,
            RE::BSTEventSource<RE::TESHitEvent>*) override;

    private:
        EventCollector() = default;

        bool _registered{ false };

        // 辅助：判断是否为玩家相关事件
        bool IsPlayerEvent(RE::TESObjectREFR* ref) const;
    };
}