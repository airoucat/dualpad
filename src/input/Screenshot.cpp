#include "pch.h"
#include "input/Screenshot.h"
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
    std::string TakeScreenshot()
    {
        logger::info("[Screenshot] Taking DirectX screenshot...");

        try {
            // 获取 Renderer
            auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
            if (!renderer) {
                logger::error("[Screenshot] Failed to get Renderer");
                return "";
            }

            auto& data = renderer->GetRuntimeData();
            auto* device = data.forwarder;
            auto* context = data.context;

            if (!device || !context) {
                logger::error("[Screenshot] D3D11 Device or Context is null");
                return "";
            }

            // 获取后台缓冲区
            // 通过 Renderer 的第一个 RenderWindow 获取 SwapChain
            auto& renderWindow = data.renderWindows[0];
            auto* swapChain = renderWindow.swapChain;
            if (!swapChain) {
                logger::error("[Screenshot] SwapChain is null");
                return "";
            }
            // 获取后台缓冲区纹理
            ComPtr<REX::W32::ID3D11Texture2D> backBuffer;
            // 转换 GUID 类型
            const auto& iid = reinterpret_cast<const REX::W32::GUID&>(__uuidof(REX::W32::ID3D11Texture2D));
            HRESULT hr = swapChain->GetBuffer(0, iid,
                reinterpret_cast<void**>(backBuffer.GetAddressOf()));
            if (FAILED(hr)) {
                logger::error("[Screenshot] Failed to get back buffer: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            // 获取纹理描述
            REX::W32::D3D11_TEXTURE2D_DESC desc;
            backBuffer->GetDesc(&desc);

            logger::info("[Screenshot] Back buffer size: {}x{}", desc.width, desc.height);

            // 创建可读取的 Staging 纹理
            desc.usage = REX::W32::D3D11_USAGE_STAGING;
            desc.bindFlags = 0;
            desc.cpuAccessFlags = REX::W32::D3D11_CPU_ACCESS_READ;
            desc.miscFlags = 0;

            ComPtr<REX::W32::ID3D11Texture2D> stagingTexture;
            hr = device->CreateTexture2D(&desc, nullptr, stagingTexture.GetAddressOf());
            if (FAILED(hr)) {
                logger::error("[Screenshot] Failed to create staging texture: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            // 复制后台缓冲区到 Staging 纹理
            context->CopyResource(stagingTexture.Get(), backBuffer.Get());

            // 映射纹理以读取数据
            REX::W32::D3D11_MAPPED_SUBRESOURCE mapped;
            hr = context->Map(stagingTexture.Get(), 0, REX::W32::D3D11_MAP_READ, 0, &mapped);
            if (FAILED(hr)) {
                logger::error("[Screenshot] Failed to map texture: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            // 生成文件名
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::tm tm;
            localtime_s(&tm, &time);

            std::string filename = std::format("Screenshot_{:04d}{:02d}{:02d}_{:02d}{:02d}{:02d}_{:03d}.png",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));

            // 保存路径
            char* userProfile = nullptr;
            size_t len = 0;
            _dupenv_s(&userProfile, &len, "USERPROFILE");
            if (!userProfile) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to get USERPROFILE");
                return "";
            }

            fs::path documentsPath = fs::path(userProfile) / "Documents" / "My Games" / "Skyrim Special Edition" / "Screenshots";
            free(userProfile);
            fs::create_directories(documentsPath);
            fs::path fullPath = documentsPath / filename;

            // 使用 WIC 保存为 PNG
            ComPtr<IWICImagingFactory> wicFactory;
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(wicFactory.GetAddressOf()));
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to create WIC factory: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            ComPtr<IWICStream> stream;
            hr = wicFactory->CreateStream(stream.GetAddressOf());
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to create WIC stream: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = stream->InitializeFromFilename(fullPath.wstring().c_str(), GENERIC_WRITE);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to initialize stream: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            ComPtr<IWICBitmapEncoder> encoder;
            hr = wicFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to create encoder: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to initialize encoder: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            ComPtr<IWICBitmapFrameEncode> frame;
            hr = encoder->CreateNewFrame(frame.GetAddressOf(), nullptr);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to create frame: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = frame->Initialize(nullptr);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to initialize frame: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = frame->SetSize(desc.width, desc.height);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to set frame size: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
            hr = frame->SetPixelFormat(&format);
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to set pixel format: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = frame->WritePixels(desc.height, mapped.rowPitch,
                mapped.rowPitch * desc.height, static_cast<BYTE*>(mapped.data));
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to write pixels: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = frame->Commit();
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to commit frame: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            hr = encoder->Commit();
            if (FAILED(hr)) {
                context->Unmap(stagingTexture.Get(), 0);
                logger::error("[Screenshot] Failed to commit encoder: {:X}", static_cast<uint32_t>(hr));
                return "";
            }

            context->Unmap(stagingTexture.Get(), 0);

            logger::info("[Screenshot] Saved to: {}", fullPath.string());
            return fullPath.string();

        }
        catch (const std::exception& e) {
            logger::error("[Screenshot] Exception: {}", e.what());
            return "";
        }
    }
}