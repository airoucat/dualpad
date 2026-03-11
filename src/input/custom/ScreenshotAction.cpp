#include "pch.h"
#include "input/custom/ScreenshotAction.h"
#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <ShlObj_core.h>
#include <filesystem>
#include <format>
#include <limits>
#include <cstring>
#include <system_error>
#include <wincodec.h>
#include <wrl/client.h>

namespace logger = SKSE::log;
namespace fs = std::filesystem;

using Microsoft::WRL::ComPtr;

namespace dualpad::input::custom
{
    namespace
    {
        constexpr std::uint32_t BYTES_PER_PIXEL = 4;

        class ComApartmentGuard
        {
        public:
            explicit ComApartmentGuard(DWORD flags)
            {
                _result = CoInitializeEx(nullptr, flags);
                _initialized = SUCCEEDED(_result);
            }

            ~ComApartmentGuard()
            {
                if (_initialized) {
                    CoUninitialize();
                }
            }

            [[nodiscard]] HRESULT Result() const
            {
                return _result;
            }

        private:
            HRESULT _result{ E_FAIL };
            bool _initialized{ false };
        };

        class MappedTextureGuard
        {
        public:
            MappedTextureGuard(
                REX::W32::ID3D11DeviceContext* context,
                REX::W32::ID3D11Texture2D* texture,
                const REX::W32::D3D11_MAPPED_SUBRESOURCE& mapped) noexcept :
                _context(context),
                _texture(texture),
                _mapped(mapped)
            {}

            ~MappedTextureGuard()
            {
                if (_context && _texture) {
                    _context->Unmap(_texture, 0);
                }
            }

            [[nodiscard]] const std::uint8_t* Data() const noexcept
            {
                return static_cast<const std::uint8_t*>(_mapped.data);
            }

            [[nodiscard]] std::uint32_t RowPitch() const noexcept
            {
                return _mapped.rowPitch;
            }

        private:
            REX::W32::ID3D11DeviceContext* _context{ nullptr };
            REX::W32::ID3D11Texture2D* _texture{ nullptr };
            REX::W32::D3D11_MAPPED_SUBRESOURCE _mapped{};
        };

        enum class PixelLayout
        {
            Bgra,
            Bgrx,
            Rgba,
            Unsupported
        };

        [[nodiscard]] PixelLayout GetPixelLayout(REX::W32::DXGI_FORMAT format)
        {
            switch (format) {
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM:
            case REX::W32::DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                return PixelLayout::Bgra;
            case REX::W32::DXGI_FORMAT_B8G8R8X8_UNORM:
            case REX::W32::DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                return PixelLayout::Bgrx;
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM:
            case REX::W32::DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                return PixelLayout::Rgba;
            default:
                return PixelLayout::Unsupported;
            }
        }

        [[nodiscard]] bool CopyToPackedBgra(
            std::vector<std::uint8_t>& destination,
            std::uint32_t destinationRowPitch,
            const std::uint8_t* source,
            std::uint32_t sourceRowPitch,
            std::uint32_t width,
            std::uint32_t height,
            PixelLayout layout)
        {
            if (!source || sourceRowPitch < (width * BYTES_PER_PIXEL)) {
                return false;
            }

            for (std::uint32_t y = 0; y < height; ++y) {
                const auto* sourceRow = source + (static_cast<std::size_t>(y) * sourceRowPitch);
                auto* destinationRow = destination.data() + (static_cast<std::size_t>(y) * destinationRowPitch);

                switch (layout) {
                case PixelLayout::Bgra:
                    std::memcpy(destinationRow, sourceRow, destinationRowPitch);
                    break;
                case PixelLayout::Bgrx:
                    std::memcpy(destinationRow, sourceRow, destinationRowPitch);
                    for (std::uint32_t x = 0; x < width; ++x) {
                        destinationRow[(static_cast<std::size_t>(x) * BYTES_PER_PIXEL) + 3] = 0xFF;
                    }
                    break;
                case PixelLayout::Rgba:
                    for (std::uint32_t x = 0; x < width; ++x) {
                        const auto sourceIndex = static_cast<std::size_t>(x) * BYTES_PER_PIXEL;
                        destinationRow[sourceIndex + 0] = sourceRow[sourceIndex + 2];
                        destinationRow[sourceIndex + 1] = sourceRow[sourceIndex + 1];
                        destinationRow[sourceIndex + 2] = sourceRow[sourceIndex + 0];
                        destinationRow[sourceIndex + 3] = sourceRow[sourceIndex + 3];
                    }
                    break;
                case PixelLayout::Unsupported:
                    return false;
                }
            }

            return true;
        }

    }

    ScreenshotActionService& ScreenshotActionService::GetSingleton()
    {
        static ScreenshotActionService instance;
        return instance;
    }

    ScreenshotActionService::~ScreenshotActionService()
    {
        Stop();
    }

    void ScreenshotActionService::Start()
    {
        {
            std::scoped_lock stateLock(_stateMutex);
            if (_startupState == StartupState::Ready || _startupState == StartupState::Starting) {
                return;
            }
        }

        std::wstring saveDirectory = ResolveSaveDirectory();
        if (saveDirectory.empty()) {
            std::scoped_lock stateLock(_stateMutex);
            _startupState = StartupState::Failed;
            return;
        }

        if (_thread.joinable()) {
            _thread.join();
        }

        {
            std::scoped_lock stateLock(_stateMutex);
            _saveDirectory = std::move(saveDirectory);
            _startupState = StartupState::Starting;
        }

        {
            std::scoped_lock queueLock(_queueMutex);
            _stopRequested = false;
        }

        logger::info("[DualPad][CustomAction][Screenshot] Starting screenshot service");

        _thread = std::jthread([this] {
            SaverThread();
            });

        std::unique_lock stateLock(_stateMutex);
        _startupCv.wait(stateLock, [this] { return _startupState != StartupState::Starting; });

        if (_startupState == StartupState::Ready) {
            logger::info("[DualPad][CustomAction][Screenshot] Screenshot service is ready");
        }
        else {
            logger::error("[DualPad][CustomAction][Screenshot] Screenshot service failed to initialize");
        }
    }

    void ScreenshotActionService::Stop()
    {
        {
            std::scoped_lock stateLock(_stateMutex);
            if (_startupState == StartupState::Stopped) {
                return;
            }

            _startupState = StartupState::Stopping;
        }

        logger::info("[DualPad][CustomAction][Screenshot] Stopping screenshot service");

        {
            std::scoped_lock queueLock(_queueMutex);
            _stopRequested = true;
        }

        _queueCv.notify_all();

        if (_thread.joinable()) {
            _thread.join();
        }

        {
            std::scoped_lock queueLock(_queueMutex);
            _queue.clear();
            _stopRequested = false;
        }

        {
            std::scoped_lock captureLock(_captureMutex);
            _lastCaptureTime = {};
        }

        {
            std::scoped_lock stateLock(_stateMutex);
            _startupState = StartupState::Stopped;
        }

        logger::info("[DualPad][CustomAction][Screenshot] Screenshot service stopped");
    }

    bool ScreenshotActionService::IsReady() const
    {
        std::scoped_lock stateLock(_stateMutex);
        return _startupState == StartupState::Ready;
    }

    ScreenshotRequestResult ScreenshotActionService::RequestCapture()
    {
        ScreenshotRequestResult result{};

        {
            std::scoped_lock stateLock(_stateMutex);
            if (_startupState != StartupState::Ready) {
                return result;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        {
            std::scoped_lock captureLock(_captureMutex);
            if (_lastCaptureTime != std::chrono::steady_clock::time_point{}) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastCaptureTime).count();
                if (elapsed < MIN_CAPTURE_INTERVAL_MS) {
                    result.status = ScreenshotRequestStatus::Debounced;
                    return result;
                }
            }
        }

        try {
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (!renderer) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to get renderer");
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            auto& runtimeData = renderer->GetRuntimeData();
            auto* device = runtimeData.forwarder;
            auto* context = runtimeData.context;
            if (!device || !context) {
                logger::error("[DualPad][CustomAction][Screenshot] D3D11 device or context is null");
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            auto* swapChain = runtimeData.renderWindows[0].swapChain;
            if (!swapChain) {
                logger::error("[DualPad][CustomAction][Screenshot] Swap chain is null");
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            ComPtr<REX::W32::ID3D11Texture2D> backBuffer;
            const auto& textureIid = reinterpret_cast<const REX::W32::GUID&>(__uuidof(REX::W32::ID3D11Texture2D));
            HRESULT hr = swapChain->GetBuffer(0, textureIid, reinterpret_cast<void**>(backBuffer.GetAddressOf()));
            if (FAILED(hr)) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to get back buffer: {:X}",
                    static_cast<std::uint32_t>(hr));
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            REX::W32::D3D11_TEXTURE2D_DESC description;
            backBuffer->GetDesc(&description);

            const PixelLayout pixelLayout = GetPixelLayout(description.format);
            if (pixelLayout == PixelLayout::Unsupported) {
                logger::error("[DualPad][CustomAction][Screenshot] Unsupported back buffer format: {}",
                    static_cast<std::uint32_t>(description.format));
                result.status = ScreenshotRequestStatus::UnsupportedFormat;
                return result;
            }

            description.usage = REX::W32::D3D11_USAGE_STAGING;
            description.bindFlags = 0;
            description.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
            description.miscFlags = 0;

            ComPtr<REX::W32::ID3D11Texture2D> stagingTexture;
            hr = device->CreateTexture2D(&description, nullptr, stagingTexture.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to create staging texture: {:X}",
                    static_cast<std::uint32_t>(hr));
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            ScreenshotData screenshot;
            screenshot.width = description.width;
            screenshot.height = description.height;
            screenshot.rowPitch = description.width * BYTES_PER_PIXEL;
            screenshot.filename = GenerateFilename();
            screenshot.pixels.resize(static_cast<std::size_t>(screenshot.rowPitch) * screenshot.height);

            context->CopyResource(stagingTexture.Get(), backBuffer.Get());

            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = context->Map(stagingTexture.Get(), 0, REX::W32::D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to map staging texture: {:X}",
                    static_cast<std::uint32_t>(hr));
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            MappedTextureGuard mappedTexture(context, stagingTexture.Get(), mapped);
            if (!CopyToPackedBgra(
                    screenshot.pixels,
                    screenshot.rowPitch,
                    mappedTexture.Data(),
                    mappedTexture.RowPitch(),
                    screenshot.width,
                    screenshot.height,
                    pixelLayout)) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to normalize mapped pixels");
                result.status = ScreenshotRequestStatus::CaptureFailed;
                return result;
            }

            {
                std::scoped_lock queueLock(_queueMutex);
                if (_queue.size() >= _maxQueueSize) {
                    logger::warn("[DualPad][CustomAction][Screenshot] Queue is full ({}/{}), dropping oldest request",
                        _queue.size(),
                        _maxQueueSize);
                    _queue.pop_front();
                }

                _queue.push_back(std::move(screenshot));
                result.queueDepth = _queue.size();
                result.filename = _queue.back().filename;
            }

            {
                std::scoped_lock captureLock(_captureMutex);
                _lastCaptureTime = now;
            }

            result.status = ScreenshotRequestStatus::Queued;
            _queueCv.notify_one();
            return result;
        }
        catch (const std::exception& e) {
            logger::error("[DualPad][CustomAction][Screenshot] Exception during capture: {}", e.what());
            result.status = ScreenshotRequestStatus::CaptureFailed;
            return result;
        }
        catch (...) {
            logger::error("[DualPad][CustomAction][Screenshot] Unknown exception during capture");
            result.status = ScreenshotRequestStatus::CaptureFailed;
            return result;
        }
    }

    void ScreenshotActionService::SaverThread()
    {
        logger::info("[DualPad][CustomAction][Screenshot] Saver thread starting");

        try {
            const ComApartmentGuard apartment(COINIT_MULTITHREADED);
            if (FAILED(apartment.Result())) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to initialize COM: {:X}",
                    static_cast<std::uint32_t>(apartment.Result()));

                {
                    std::scoped_lock stateLock(_stateMutex);
                    _startupState = StartupState::Failed;
                }
                _startupCv.notify_all();
                return;
            }

            ComPtr<IWICImagingFactory> wicFactory;
            HRESULT hr = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(wicFactory.GetAddressOf()));
            if (FAILED(hr)) {
                logger::error("[DualPad][CustomAction][Screenshot] Failed to create WIC factory: {:X}",
                    static_cast<std::uint32_t>(hr));

                {
                    std::scoped_lock stateLock(_stateMutex);
                    _startupState = StartupState::Failed;
                }
                _startupCv.notify_all();
                return;
            }

            std::wstring saveDirectory;
            {
                std::scoped_lock stateLock(_stateMutex);
                saveDirectory = _saveDirectory;
                _startupState = StartupState::Ready;
            }
            _startupCv.notify_all();

            for (;;) {
                ScreenshotData screenshot;

                {
                    std::unique_lock queueLock(_queueMutex);
                    _queueCv.wait(queueLock, [this] { return _stopRequested || !_queue.empty(); });

                    if (_stopRequested && _queue.empty()) {
                        break;
                    }

                    screenshot = std::move(_queue.front());
                    _queue.pop_front();
                }

                if (!SaveToFile(*wicFactory.Get(), saveDirectory, screenshot)) {
                    logger::error("[DualPad][CustomAction][Screenshot] Failed to save screenshot: {}",
                        screenshot.filename);
                }
            }
        }
        catch (const std::exception& e) {
            logger::error("[DualPad][CustomAction][Screenshot] Saver thread exception: {}", e.what());

            {
                std::scoped_lock stateLock(_stateMutex);
                if (_startupState == StartupState::Starting) {
                    _startupState = StartupState::Failed;
                }
            }
            _startupCv.notify_all();
        }
        catch (...) {
            logger::error("[DualPad][CustomAction][Screenshot] Saver thread terminated with an unknown exception");

            {
                std::scoped_lock stateLock(_stateMutex);
                if (_startupState == StartupState::Starting) {
                    _startupState = StartupState::Failed;
                }
            }
            _startupCv.notify_all();
        }

        {
            std::scoped_lock stateLock(_stateMutex);
            _startupState = StartupState::Stopped;
        }

        logger::info("[DualPad][CustomAction][Screenshot] Saver thread stopped");
    }

    bool ScreenshotActionService::SaveToFile(
        IWICImagingFactory& wicFactory,
        const std::wstring& saveDirectory,
        const ScreenshotData& data) const
    {
        if (saveDirectory.empty()) {
            logger::error("[DualPad][CustomAction][Screenshot] Save directory is empty");
            return false;
        }

        if (data.width == 0 || data.height == 0 || data.rowPitch == 0 || data.pixels.empty()) {
            logger::error("[DualPad][CustomAction][Screenshot] Screenshot payload is empty");
            return false;
        }

        if (data.pixels.size() > static_cast<std::size_t>(std::numeric_limits<UINT>::max())) {
            logger::error("[DualPad][CustomAction][Screenshot] Screenshot payload is too large to encode");
            return false;
        }

        const fs::path fullPath = fs::path(saveDirectory) / fs::path(data.filename);

        ComPtr<IWICStream> stream;
        HRESULT hr = wicFactory.CreateStream(stream.GetAddressOf());
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to create stream: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = stream->InitializeFromFilename(fullPath.c_str(), GENERIC_WRITE);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to initialize stream: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        ComPtr<IWICBitmapEncoder> encoder;
        hr = wicFactory.CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to create encoder: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to initialize encoder: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        ComPtr<IWICBitmapFrameEncode> frame;
        hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to create frame: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = frame->Initialize(nullptr);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to initialize frame: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = frame->SetSize(data.width, data.height);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to set size: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        hr = frame->SetPixelFormat(&format);
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to set format: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = frame->WritePixels(
            data.height,
            data.rowPitch,
            static_cast<UINT>(data.pixels.size()),
            const_cast<BYTE*>(data.pixels.data()));
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to write pixels: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = frame->Commit();
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to commit frame: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        hr = encoder->Commit();
        if (FAILED(hr)) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to commit encoder: {:X}",
                static_cast<std::uint32_t>(hr));
            return false;
        }

        logger::info("[DualPad][CustomAction][Screenshot] Screenshot saved: {}", fullPath.string());
        return true;
    }

    std::wstring ScreenshotActionService::ResolveSaveDirectory() const
    {
        PWSTR documentsPath = nullptr;
        const HRESULT hr = SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documentsPath);
        if (FAILED(hr) || !documentsPath) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to resolve Documents folder: {:X}",
                static_cast<std::uint32_t>(hr));
            if (documentsPath) {
                CoTaskMemFree(documentsPath);
            }
            return {};
        }

        fs::path saveDirectory = fs::path(documentsPath) / "My Games" / "Skyrim Special Edition" / "Screenshots";
        CoTaskMemFree(documentsPath);

        std::error_code error;
        fs::create_directories(saveDirectory, error);
        if (error) {
            logger::error("[DualPad][CustomAction][Screenshot] Failed to create screenshot directory '{}': {}",
                saveDirectory.string(),
                error.message());
            return {};
        }

        return saveDirectory.native();
    }

    std::string ScreenshotActionService::GenerateFilename() const
    {
        const auto now = std::chrono::system_clock::now();
        const auto timestamp = std::chrono::system_clock::to_time_t(now);
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm localTime{};
        localtime_s(&localTime, &timestamp);

        return std::format("Screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}.png",
            localTime.tm_year + 1900,
            localTime.tm_mon + 1,
            localTime.tm_mday,
            localTime.tm_hour,
            localTime.tm_min,
            localTime.tm_sec,
            static_cast<int>(milliseconds.count()));
    }
}
