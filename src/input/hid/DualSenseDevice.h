#pragma once

#include "input/hid/HidTransport.h"

#include <array>

namespace dualpad::input
{
    class DualSenseDevice
    {
    public:
        bool Open();
        void Close();
        bool IsOpen() const;

        bool ReadPacket(RawInputPacket& outPacket);
        TransportType GetTransportType() const;
        ReadStatus GetLastReadStatus() const;
        hid_device* GetNativeHandle() const;

    private:
        void MaybeInferTransport(const RawInputPacket& packet);

        HidTransport _transport{};
        std::array<std::uint8_t, 128> _buffer{};
        std::uint64_t _sequence{ 0 };
        ReadStatus _lastReadStatus{ ReadStatus::None };
    };
}
