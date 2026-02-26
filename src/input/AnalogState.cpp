#include "pch.h"
#include "input/AnalogState.h"

namespace dualpad::input
{
    AnalogState& AnalogState::GetSingleton()
    {
        static AnalogState s;
        return s;
    }

    void AnalogState::Reset()
    {
        std::scoped_lock lk(_mtx);
        _state = {};
    }

    void AnalogState::Update(float lx, float ly, float rx, float ry, float l2, float r2)
    {
        std::scoped_lock lk(_mtx);
        _state.lx = lx;
        _state.ly = ly;
        _state.rx = rx;
        _state.ry = ry;
        _state.l2 = l2;
        _state.r2 = r2;
        ++_state.seq;
    }

    AnalogSnapshot AnalogState::Read() const
    {
        std::scoped_lock lk(_mtx);
        return _state;
    }
}