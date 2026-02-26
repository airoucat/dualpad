#pragma once
#include <cstdint>
#include <mutex>

namespace dualpad::input
{
    struct AnalogSnapshot
    {
        float lx{ 0.0f };  // [-1,1]
        float ly{ 0.0f };  // [-1,1]
        float rx{ 0.0f };  // [-1,1]
        float ry{ 0.0f };  // [-1,1]
        float l2{ 0.0f };  // [0,1]
        float r2{ 0.0f };  // [0,1]
        std::uint64_t seq{ 0 };
    };

    class AnalogState
    {
    public:
        static AnalogState& GetSingleton();

        void Reset();

        void Update(float lx, float ly, float rx, float ry, float l2, float r2);

        AnalogSnapshot Read() const;

    private:
        AnalogState() = default;

        mutable std::mutex _mtx;
        AnalogSnapshot _state{};
    };
}