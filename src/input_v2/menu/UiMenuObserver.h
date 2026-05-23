#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace dualpad::input_v2::menu
{
    enum class ObserverCompleteness : std::uint8_t
    {
        Complete = 0,
        Partial,
        Unavailable
    };

    struct ObservedMenuNode
    {
        std::uintptr_t menuPtr{ 0 };
        std::string menuName;
        std::uint32_t menuFlagsValue{ 0 };
        std::uint32_t inputContextValue{ 0 };
        std::int32_t depthPriority{ 0 };
        std::uintptr_t delegatePtr{ 0 };
        std::uintptr_t moviePtr{ 0 };
        std::uint32_t observationOrder{ 0 };
    };

    struct ObservedMenuSnapshot
    {
        ObserverCompleteness completeness{ ObserverCompleteness::Complete };
        std::vector<ObservedMenuNode> nodes;
        std::uint64_t eventSequence{ 0 };
    };

    class UiMenuObserver
    {
    public:
        static UiMenuObserver& GetSingleton();

        void MarkMenuEvent(std::string_view menuName, bool opening);
        bool IsDirty() const;
        void ClearDirty();

        ObservedMenuSnapshot Capture();
        void Publish(ObservedMenuSnapshot snapshot);
        ObservedMenuSnapshot GetPublishedSnapshot() const;
        void ResetForTests();

    private:
        mutable std::mutex _mutex;
        bool _dirty{ false };
        std::uint64_t _eventSequence{ 0 };
        ObservedMenuSnapshot _published{};
    };
}
