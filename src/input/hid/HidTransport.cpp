#include "pch.h"
#include "input/hid/HidTransport.h"

#include <Windows.h>
#include <hidapi/hidapi.h>

#include <SKSE/SKSE.h>
#include <algorithm>
#include <cctype>
#include <string>

namespace logger = SKSE::log;

namespace dualpad::input
{
    namespace
    {
        inline constexpr std::uint16_t kSonyVid = 0x054C;
        inline constexpr std::uint16_t kPidDualSense = 0x0CE6;
        inline constexpr std::uint16_t kPidDualSenseEdge = 0x0DF2;

        std::string WideToUtf8(const wchar_t* wstr)
        {
            if (!wstr) {
                return "unknown";
            }

            const int needed = ::WideCharToMultiByte(
                CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
            if (needed <= 1) {
                return "unknown";
            }

            std::string out(static_cast<std::size_t>(needed), '\0');
            ::WideCharToMultiByte(
                CP_UTF8, 0, wstr, -1, out.data(), needed, nullptr, nullptr);
            out.pop_back();
            return out;
        }

        TransportType GuessTransportFromPath(const char* path)
        {
            if (!path) {
                return TransportType::Unknown;
            }

            std::string lowered(path);
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (lowered.find("bth") != std::string::npos ||
                lowered.find("bluetooth") != std::string::npos) {
                return TransportType::Bluetooth;
            }

            if (lowered.find("usb") != std::string::npos) {
                return TransportType::USB;
            }

            return TransportType::Unknown;
        }
    }

    bool HidTransport::InitializeApi()
    {
        if (hid_init() != 0) {
            logger::error("[DualPad][HID] hid_init failed");
            return false;
        }

        return true;
    }

    void HidTransport::ShutdownApi()
    {
        hid_exit();
    }

    bool HidTransport::OpenFirstDualSense()
    {
        if (_device) {
            return true;
        }

        hid_device_info* infos = hid_enumerate(kSonyVid, 0x0);
        hid_device_info* current = infos;
        while (current) {
            const bool isDualSense =
                current->vendor_id == kSonyVid &&
                (current->product_id == kPidDualSense || current->product_id == kPidDualSenseEdge);

            if (isDualSense && current->path) {
                hid_device* candidate = hid_open_path(current->path);
                if (candidate) {
                    hid_set_nonblocking(candidate, 1);
                    _device = candidate;
                    _transport = GuessTransportFromPath(current->path);
                    _vendorId = current->vendor_id;
                    _productId = current->product_id;

                    logger::info(
                        "[DualPad][HID] Device opened transport={} vid=0x{:04X} pid=0x{:04X}",
                        ToString(_transport),
                        _vendorId,
                        _productId);
                    hid_free_enumeration(infos);
                    return true;
                }
            }

            current = current->next;
        }

        hid_free_enumeration(infos);
        return false;
    }

    void HidTransport::Close()
    {
        if (!_device) {
            return;
        }

        hid_close(_device);
        _device = nullptr;
        _transport = TransportType::Unknown;
        _vendorId = 0;
        _productId = 0;
    }

    bool HidTransport::IsOpen() const
    {
        return _device != nullptr;
    }

    ReadStatus HidTransport::Read(std::span<std::uint8_t> buffer, std::size_t& outBytes)
    {
        outBytes = 0;
        if (!_device) {
            return ReadStatus::Disconnected;
        }

        const int read = hid_read_timeout(_device, buffer.data(), buffer.size(), 8);
        if (read > 0) {
            outBytes = static_cast<std::size_t>(read);
            return ReadStatus::Ok;
        }

        if (read == 0) {
            return ReadStatus::Timeout;
        }

        logger::warn("[DualPad][HID] Read failed: {}", WideToUtf8(hid_error(_device)));
        return ReadStatus::Disconnected;
    }

    TransportType HidTransport::GetTransportType() const
    {
        return _transport;
    }

    void HidTransport::SetTransportType(TransportType transport)
    {
        _transport = transport;
    }

    hid_device* HidTransport::GetNativeHandle() const
    {
        return _device;
    }
}
