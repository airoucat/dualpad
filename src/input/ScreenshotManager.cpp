#include "pch.h"
#include "input/ScreenshotManager.h"
#include <SKSE/SKSE.h>
#include <RE/Skyrim.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include <chrono>
#include <format>

#pragma comment(lib, "windowscodecs.lib")

namespace logger = SKSE::log;
namespace fs = std::filesystem;

using Microsoft::WRL::ComPtr;

namespace dualpad::utils
{
    ScreenshotManager& ScreenshotManager::GetSingleton()
    {
        static ScreenshotManager instance;
        return instance;
    }

    ScreenshotManager::~ScreenshotManager()
    {
        Stop();

        if (_wicFactory) {
            _wicFactory->Release();
            _wicFactory = nullptr;
        }
    }

    void ScreenshotManager::Start()
    {
        if (_running.exchange(true)) {
            return;
        }

        logger::info("[ScreenshotMgr] Starting background saver thread");

        {
            // WIC is created once and reused for every save request.
            std::scoped_lock lock(_wicMutex);
            if (!_wicFactory) {
                HRESULT hr = CoCreateInstance(
                    CLSID_WICImagingFactory,
                    nullptr,
                    CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&_wicFactory)
                );

                if (FAILED(hr)) {
                    logger::error("[ScreenshotMgr] Failed to create WIC factory: {:X}", static_cast<uint32_t>(hr));
                    _running = false;
                    return;
                }

                logger::info("[ScreenshotMgr] WIC factory created and cached");
            }
        }

        _thread = std::jthread([this](std::stop_token st) {
            (void)st;
            SaverThread();
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        _initialized = true;

        logger::info("[ScreenshotMgr] Initialization complete");
    }

    void ScreenshotManager::Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }

        logger::info("[ScreenshotMgr] Stopping background saver thread");

        size_t remainingCount = 0;
        {
            std::scoped_lock lock(_mutex);
            remainingCount = _queue.size();
        }

        if (remainingCount > 0) {
            logger::info("[ScreenshotMgr] Waiting for {} screenshots to be saved...", remainingCount);

            auto startTime = std::chrono::steady_clock::now();
            const auto timeout = std::chrono::seconds(10);

            while (true) {
                {
                    std::scoped_lock lock(_mutex);
                    if (_queue.empty()) {
                        logger::info("[ScreenshotMgr] All screenshots saved");
                        break;
                    }
                }

                auto elapsed = std::chrono::steady_clock::now() - startTime;
                if (elapsed > timeout) {
                    std::scoped_lock lock(_mutex);
                    logger::warn("[ScreenshotMgr] Timeout waiting for screenshots, {} remaining", _queue.size());
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        _cv.notify_all();

        if (_thread.joinable()) {
            _thread.join();
        }

        _initialized = false;

        logger::info("[ScreenshotMgr] Background saver thread stopped");
    }

    bool ScreenshotManager::CaptureFrame()
    {
        // Debounce repeated requests before touching the renderer.
        {
            std::scoped_lock lock(_captureMutex);
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _lastCaptureTime);
            if (elapsed.count() < MIN_CAPTURE_INTERVAL_MS) {
                logger::trace("[ScreenshotMgr] Screenshot request ignored (debounced, {}ms since last)",
                    elapsed.count());
                return false;
            }
            _lastCaptureTime = now;
        }

        if (!_initialized) {
            logger::warn("[ScreenshotMgr] Screenshot manager not initialized yet, ignoring request");
            return false;
        }
        if (!_running) {
            logger::warn("[ScreenshotMgr] Screenshot manager not running, ignoring request");
            return false;
        }

        try {

            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (!renderer) {
                logger::error("[ScreenshotMgr] Failed to get Renderer");
                return false;
            }

            auto& data = renderer->GetRuntimeData();
            auto* device = data.forwarder;
            auto* context = data.context;

            if (!device || !context) {
                logger::error("[ScreenshotMgr] D3D11 Device or Context is null");
                return false;
            }

            auto& renderWindow = data.renderWindows[0];
            auto* swapChain = renderWindow.swapChain;

            if (!swapChain) {
                logger::error("[ScreenshotMgr] SwapChain is null");
                return false;
            }

            ComPtr<REX::W32::ID3D11Texture2D> backBuffer;
            const auto& iid = reinterpret_cast<const REX::W32::GUID&>(__uuidof(REX::W32::ID3D11Texture2D));
            HRESULT hr = swapChain->GetBuffer(0, iid, reinterpret_cast<void**>(backBuffer.GetAddressOf()));

            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to get back buffer: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            REX::W32::D3D11_TEXTURE2D_DESC desc;
            backBuffer->GetDesc(&desc);

            desc.usage = REX::W32::D3D11_USAGE_STAGING;
            desc.bindFlags = 0;
            desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
            desc.miscFlags = 0;

            ComPtr<REX::W32::ID3D11Texture2D> stagingTexture;
            hr = device->CreateTexture2D(&desc, nullptr, stagingTexture.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to create staging texture: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            context->CopyResource(stagingTexture.Get(), backBuffer.Get());

            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped;
            hr = context->Map(stagingTexture.Get(), 0, REX::W32::D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to map texture: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            ScreenshotData screenshot;
            screenshot.width = desc.width;
            screenshot.height = desc.height;
            screenshot.rowPitch = mapped.rowPitch;
            screenshot.filename = GenerateFilename();

            size_t dataSize = mapped.rowPitch * desc.height;
            screenshot.pixels.resize(dataSize);
            memcpy(screenshot.pixels.data(), mapped.data, dataSize);

            context->Unmap(stagingTexture.Get(), 0);

            {
                // The queue is bounded so screenshot hotkeys cannot grow memory without limit.
                std::scoped_lock lock(_mutex);

                if (_queue.size() >= _maxQueueSize) {
                    logger::warn("[ScreenshotMgr] Queue is full ({}/{}), dropping oldest screenshot",
                        _queue.size(), _maxQueueSize);
                    _queue.pop();
                }
                _queue.push(std::move(screenshot));

                logger::info("[ScreenshotMgr] Screenshot captured ({}x{}), queue size: {}/{}",
                    desc.width, desc.height, _queue.size(), _maxQueueSize);
            }

            _cv.notify_one();

            return true;

        }
        catch (const std::exception& e) {
            logger::error("[ScreenshotMgr] Exception during capture: {}", e.what());
            return false;
        }
    }

    size_t ScreenshotManager::GetQueueSize() const
    {
        std::scoped_lock lock(_mutex);
        return _queue.size();
    }

    void ScreenshotManager::SaverThread()
    {
        logger::info("[ScreenshotMgr] Saver thread started");

        while (_running) {
            ScreenshotData screenshot;

            // Wait until new work arrives or shutdown begins.
            {
                std::unique_lock lock(_mutex);
                _cv.wait(lock, [this] { return !_queue.empty() || !_running; });

                if (!_running && _queue.empty()) {
                    break;
                }

                if (_queue.empty()) {
                    continue;
                }

                screenshot = std::move(_queue.front());
                _queue.pop();
            }

            if (SaveToFile(screenshot)) {
                logger::info("[ScreenshotMgr] Screenshot saved: {}", screenshot.filename);
            }
            else {
                logger::error("[ScreenshotMgr] Failed to save screenshot: {}", screenshot.filename);
            }
        }

        logger::info("[ScreenshotMgr] Saver thread stopped");
    }

    bool ScreenshotManager::SaveToFile(const ScreenshotData& data)
    {
        try {
            std::scoped_lock lock(_wicMutex);

            if (!_wicFactory) {
                logger::error("[ScreenshotMgr] WIC factory is null");
                return false;
            }

            fs::path fullPath = fs::path(GetSavePath()) / data.filename;

            ComPtr<IWICStream> stream;
            HRESULT hr = _wicFactory->CreateStream(stream.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to create stream: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = stream->InitializeFromFilename(fullPath.wstring().c_str(), GENERIC_WRITE);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to initialize stream: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            ComPtr<IWICBitmapEncoder> encoder;
            hr = _wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to create encoder: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to initialize encoder: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            ComPtr<IWICBitmapFrameEncode> frame;
            hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to create frame: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = frame->Initialize(nullptr);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to initialize frame: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = frame->SetSize(data.width, data.height);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to set size: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            hr = frame->SetPixelFormat(&format);
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to set format: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = frame->WritePixels(
                data.height,
                data.rowPitch,
                static_cast<UINT>(data.pixels.size()),
                const_cast<BYTE*>(data.pixels.data())
            );
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to write pixels: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = frame->Commit();
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to commit frame: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            hr = encoder->Commit();
            if (FAILED(hr)) {
                logger::error("[ScreenshotMgr] Failed to commit encoder: {:X}", static_cast<uint32_t>(hr));
                return false;
            }

            return true;

        }
        catch (const std::exception& e) {
            logger::error("[ScreenshotMgr] Exception during save: {}", e.what());
            return false;
        }
    }

    std::string ScreenshotManager::GenerateFilename()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm;
        localtime_s(&tm, &time);

        return std::format("Screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}.png",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
    }

    std::string ScreenshotManager::GetSavePath()
    {
        char* userProfile = nullptr;
        size_t len = 0;
        _dupenv_s(&userProfile, &len, "USERPROFILE");

        if (!userProfile) {
            return "";
        }

        fs::path path = fs::path(userProfile) / "Documents" / "My Games" / "Skyrim Special Edition" / "Screenshots";
        free(userProfile);

        fs::create_directories(path);
        return path.string();
    }
}
