/**
 * @see https://docs.microsoft.com/en-us/windows/apps/windows-app-sdk/set-up-your-development-environment
 */

// C++/WinRT headers
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/base.h>

// IDL headers
#include <MemoryBuffer.h> // IMemoryBufferByteAccess
#include <inspectable.h>  // IInspectable

#include <DirectXTK/DirectXHelpers.h>
#include <DirectXTex.h>
#include <cstdio>
#include <filesystem>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

//using namespace Windows::Graphics::Imaging;
using Windows::Foundation::IMemoryBufferByteAccess;
using winrt::Windows::Foundation::IMemoryBufferReference;
using winrt::Windows::Graphics::Imaging::BitmapBuffer;
using winrt::Windows::Graphics::Imaging::BitmapBufferAccessMode;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;

class app_instance_t {
    D3D_FEATURE_LEVEL device_feature_level{};
    winrt::com_ptr<ID3D11Device> device{};
    winrt::com_ptr<ID3D11DeviceContext> device_context{};
    winrt::com_ptr<ID3D10Multithread> threading{};
    winrt::com_ptr<ID3D11Texture2D> texture{};

  public:
    app_instance_t() {
        winrt::init_apartment();
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, 3,
                                        D3D11_SDK_VERSION, device.put(), &device_feature_level, device_context.put());
            FAILED(hr)) {
            spdlog::error("{}: {:#08x}", "D3D11CreateDevice", static_cast<uint32_t>(hr));
            winrt::throw_hresult(hr);
        }
        if (auto hr = device->QueryInterface(threading.put()); SUCCEEDED(hr))
            threading->SetMultithreadProtected(true);
        setup_texture2d_nv12();
    }
    ~app_instance_t() {
        winrt::uninit_apartment();
    }

    void setup_texture2d_nv12() noexcept(false) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Format = DXGI_FORMAT_NV12; // Common for Windows Webcam video sources
        desc.Width = 640;
        desc.Height = 480;
        desc.ArraySize = 1;
        desc.MipLevels = 1;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;  // read from texture in the shader
        desc.Usage = D3D11_USAGE_DYNAMIC;             // copying from CPU memory
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // write into the texture
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, texture.put()));
    }

    /// SoftwareBitmap bitmap{BitmapPixelFormat::Nv12, 640, 480, BitmapAlphaMode::Ignore};
    HRESULT on_bitmap(SoftwareBitmap bitmap) {
        BitmapBuffer buffer = bitmap.LockBuffer(BitmapBufferAccessMode::Write);
        winrt::com_ptr<IInspectable> unknown = nullptr;
        if (auto hr = buffer.CreateReference().as(winrt::guid_of<IInspectable>(), unknown.put_void()); FAILED(hr))
            return hr;
        winrt::com_ptr<IMemoryBufferByteAccess> access = nullptr;
        if (auto hr = unknown->QueryInterface(access.put()); FAILED(hr))
            return hr;
        winrt::com_ptr<ID3D11Resource> resource{};
        if (auto hr = texture->QueryInterface(resource.put()); FAILED(hr))
            return hr;
        DirectX::MapGuard mapping{device_context.get(), resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0};
        BYTE* ptr = nullptr;
        UINT32 capacity = 0;
        if (auto hr = access->GetBuffer(&ptr, &capacity); FAILED(hr))
            return hr;
        std::memcpy(mapping.pData, ptr, capacity);
        return S_OK;
    }

    [[nodiscard]] uint32_t run([[maybe_unused]] int argc, [[maybe_unused]] wchar_t* argv[],
                               [[maybe_unused]] wchar_t* envp[]) noexcept(false) {
        spdlog::info("C++/WinRT:");
        spdlog::info("  version: {:s}", CPPWINRT_VERSION); // WINRT_version
        D3D11_FEATURE_DATA_D3D11_OPTIONS features{};
        return device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &features,
                                           sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS));
    }
};

auto make_logger(const char* name, FILE* fout) noexcept(false) {
    using mutex_t = spdlog::details::console_nullmutex;
    using sink_t = spdlog::sinks::stdout_sink_base<mutex_t>;
    return std::make_shared<spdlog::logger>(name, std::make_shared<sink_t>(fout));
}

int wmain(int argc, wchar_t* argv[], wchar_t* envp[]) {
    try {
        auto logger = make_logger("test", stdout);
        logger->set_pattern("%T.%e [%L] %8t %v");
        logger->set_level(spdlog::level::level_enum::debug);
        spdlog::set_default_logger(logger);
        app_instance_t app{};
        return app.run(argc, argv, envp);
    } catch (const winrt::hresult_error& ex) {
        spdlog::error("{}", winrt::to_string(ex.message()));
        return ex.code();
    } catch (const std::exception& ex) {
        spdlog::error("{}", ex.what());
        return EXIT_FAILURE;
    }
}
