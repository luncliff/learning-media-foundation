#include <catch2/catch.hpp>

// clang-format off
#include <d3d11_4.h>
#include <d3d9.h>
#include <mfapi.h>
#include <windows.h>
#include <windowsx.h>

#include <Dxva2api.h>
#include <codecapi.h> // for [codec]
#include <evr.h>
#include <mediaobj.h> // for [dsp]
#include <mfplay.h>
#include <mmdeviceapi.h>
#include <ppl.h>
#include <shlwapi.h>
#include <wmsdkidl.h>
//#pragma comment(lib, "strmiids") // for MR_VIDEO_RENDER_SERVICE
// clang-format on

#include <filesystem>
#include <winrt/base.h>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;
using winrt::com_ptr;

fs::path get_asset_dir() noexcept;

HRESULT print_formats(ID3D11Device* device, DXGI_FORMAT format) {
    UINT flags = 0;
    if (auto hr = device->CheckFormatSupport(format, &flags); FAILED(hr)) {
        spdlog::error("{} not supported", format);
        return hr;
    }
    spdlog::info("- DXGI_FORMAT: {:#x}", static_cast<uint32_t>(format));
    auto supports = [flags](D3D11_FORMAT_SUPPORT mask) -> bool { return flags & mask; };
    spdlog::info("  D3D11_FORMAT_SUPPORT_TEXTURE2D: {}", supports(D3D11_FORMAT_SUPPORT_TEXTURE2D));
    spdlog::info("  D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT: {}",
                 supports(D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_INPUT));
    spdlog::info("  D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT: {}",
                 supports(D3D11_FORMAT_SUPPORT_VIDEO_PROCESSOR_OUTPUT));
    spdlog::info("  D3D11_FORMAT_SUPPORT_VIDEO_ENCODER: {}", supports(D3D11_FORMAT_SUPPORT_VIDEO_ENCODER));
    spdlog::info("  D3D11_FORMAT_SUPPORT_DECODER_OUTPUT: {}", supports(D3D11_FORMAT_SUPPORT_DECODER_OUTPUT));
    return S_OK;
}

/// @see https://docs.microsoft.com/en-us/windows/win32/api/d3d11_2/nn-d3d11_2-id3d11device2
struct dx11_test_case {
    D3D_FEATURE_LEVEL device_feature_level{};
    com_ptr<ID3D11Device> device{};
    com_ptr<ID3D11DeviceContext> context{};
    com_ptr<ID3D10Multithread> threading{};

  public:
    dx11_test_case() {
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        if (auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                        D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, 3,
                                        D3D11_SDK_VERSION, device.put(), &device_feature_level, context.put());
            FAILED(hr)) {
            spdlog::error("{}: {:#08x}", "D3D11CreateDevice", hr);
            FAIL(hr);
        }
        if (auto hr = device->QueryInterface(threading.put()); SUCCEEDED(hr))
            threading->SetMultithreadProtected(true);
    }
};

TEST_CASE_METHOD(dx11_test_case, "ID3D11Device FeatureLevel", "[dx11]") {
    REQUIRE(device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1);
    SECTION("11.1") {
        // https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice
        com_ptr<ID3D11Device1> device1{};
        REQUIRE(device->QueryInterface(device1.put()) == S_OK);
        {
            D3D11_FEATURE_DATA_D3D9_OPTIONS features{};
            REQUIRE(device->CheckFeatureSupport(D3D11_FEATURE_D3D9_OPTIONS, &features,
                                                sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS)) == S_OK);
        }
        {
            D3D11_FEATURE_DATA_D3D11_OPTIONS features{};
            REQUIRE(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &features,
                                                sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS)) == S_OK);
        }
    }
    SECTION("11.2") {
        // https://docs.microsoft.com/en-us/windows/win32/api/d3d11_2/nn-d3d11_2-id3d11device2
        com_ptr<ID3D11Device2> device2{};
        REQUIRE(device->QueryInterface(device2.put()) == S_OK);
        {
            D3D11_FEATURE_DATA_D3D9_OPTIONS1 features{};
            REQUIRE(device->CheckFeatureSupport(D3D11_FEATURE_D3D9_OPTIONS1, &features,
                                                sizeof(D3D11_FEATURE_DATA_D3D9_OPTIONS1)) == S_OK);
        }
        {
            D3D11_FEATURE_DATA_D3D11_OPTIONS1 features{};
            REQUIRE(device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &features,
                                                sizeof(D3D11_FEATURE_DATA_D3D11_OPTIONS1)) == S_OK);
        }
        {
            D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT features{};
            REQUIRE(device->CheckFeatureSupport(D3D11_FEATURE_D3D9_SIMPLE_INSTANCING_SUPPORT, &features,
                                                sizeof(D3D11_FEATURE_DATA_D3D9_SIMPLE_INSTANCING_SUPPORT)) == S_OK);
        }
    }
}

/**
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-buffers
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/video-subtype-guids#uncompressed-rgb-formats
 * @see https://docs.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes
 * @see https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-legacy-formats
 */
struct dxva_test_case : public dx11_test_case {
    com_ptr<IDXGIAdapter> adapter{};
    UINT dxgi_manager_token{};
    com_ptr<IMFDXGIDeviceManager> dxgi_manager{};
    HANDLE dxgi_handle{};
    com_ptr<ID3D11VideoDevice> video_device{};

  public:
    dxva_test_case() : dx11_test_case() {
        // Obtain a pointer to the IMFDXGIDeviceManager interface.
        REQUIRE(MFCreateDXGIDeviceManager(&dxgi_manager_token, dxgi_manager.put()) == S_OK);
        // Obtain a pointer to the IMFDXGIDeviceManager interface.
        HRESULT hr = dxgi_manager->GetVideoService(dxgi_handle, //
                                                   IID_ID3D11VideoDevice, video_device.put_void());
        switch (hr) {
        case MF_E_DXGI_NEW_VIDEO_DEVICE:
            dxgi_manager->CloseDeviceHandle(dxgi_handle);
            [[fallthrough]];
        case MF_E_DXGI_DEVICE_NOT_INITIALIZED:
        case E_HANDLE:
            break;
        default:
            FAIL(hr);
        }
        if (hr = dxgi_manager->ResetDevice(device.get(), dxgi_manager_token); FAILED(hr))
            FAILED(hr);
        if (hr = dxgi_manager->OpenDeviceHandle(&dxgi_handle); FAILED(hr))
            FAILED(hr);
        // retry with new dxgi_handle
        if (hr = dxgi_manager->GetVideoService(dxgi_handle, IID_ID3D11VideoDevice, video_device.put_void()); FAILED(hr))
            FAILED(hr);
    }
    ~dxva_test_case() {
        if (dxgi_handle)
            dxgi_manager->CloseDeviceHandle(dxgi_handle);
    }
};

TEST_CASE_METHOD(dxva_test_case, "ID3D11VideoDevice", "[!mayfail]") {
    SECTION("GetVideoDecoderProfile") {
        UINT count = video_device->GetVideoDecoderProfileCount();
        for (auto i = 0; i < count; ++i) {
            GUID profile{};
            auto hr = video_device->GetVideoDecoderProfile(i, &profile);
            if (FAILED(hr))
                FAIL(hr);
        }
    }
    SECTION("CreateVideoDecoder") {
        D3D11_VIDEO_DECODER_DESC desc{};
        D3D11_VIDEO_DECODER_CONFIG config{};
        UINT count = 0;
        REQUIRE(video_device->GetVideoDecoderConfigCount(&desc, &count) == 0);
        for (auto i = 0; i < count; ++i) {
            if (auto hr = video_device->GetVideoDecoderConfig(&desc, i, &config); FAILED(hr))
                FAIL(hr);
            com_ptr<ID3D11VideoDecoder> decoder{};
            REQUIRE(video_device->CreateVideoDecoder(&desc, &config, decoder.put()) == S_OK);
        }
    }
    SECTION("CreateVideoProcessor") {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputWidth = 640;
        desc.InputHeight = 480;
        desc.OutputWidth = 256;
        desc.OutputHeight = 256;
        com_ptr<ID3D11VideoProcessorEnumerator> enumerator{};
        REQUIRE(video_device->CreateVideoProcessorEnumerator(&desc, enumerator.put()) == S_OK);
        REQUIRE(enumerator);
        D3D11_VIDEO_PROCESSOR_CAPS caps{};
        REQUIRE(enumerator->GetVideoProcessorCaps(&caps) == S_OK);
        const UINT index = caps.MaxInputStreams - 1;
        com_ptr<ID3D11VideoProcessor> processor{};
        REQUIRE(video_device->CreateVideoProcessor(enumerator.get(), index, processor.put()) == S_OK);
    }
}

/// @brief For a Media Foundation transform (MFT), this step occurs during the MFT_MESSAGE_SET_D3D_MANAGER event.
/// @see https://github.com/mmaitre314/VideoEffect/tree/master/VideoEffects/VideoEffects/VideoEffects.Shared
TEST_CASE_METHOD(dxva_test_case, "DirectX Surface Buffer(MF_SA_D3D11_SHARED_WITHOUT_MUTEX)") {
    // Call MFCreateVideoSampleAllocatorEx to create the allocator object and get a pointer to the IMFVideoSampleAllocatorEx interface.
    com_ptr<IMFVideoSampleAllocatorEx> allocator{};
    REQUIRE(MFCreateVideoSampleAllocatorEx(IID_IMFVideoSampleAllocatorEx, reinterpret_cast<void**>(allocator.put())) ==
            S_OK);
    // Call IMFVideoSampleAllocator::SetDirectXManager on the allocator to set the IMFDXGIDeviceManager pointer on the allocator.
    REQUIRE(allocator->SetDirectXManager(dxgi_manager.get()) == S_OK);

    // Call MFCreateAttributes to get a pointer to the IMFAttributes interface.
    com_ptr<IMFAttributes> attrs{};
    REQUIRE(MFCreateAttributes(attrs.put(), 5) == S_OK);
    // Set the MF_SA_D3D11_USAGE and MF_SA_D3D11_BINDFLAGS attributes.
    // todo: D3D11_USAGE_DEFAULT
    attrs->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
    // todo: D3D11_BIND_DECODER, D3D11_BIND_VIDEO_ENCODER
    attrs->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    attrs->SetUINT32(MF_SA_D3D11_SHARED, TRUE);
    attrs->SetUINT32(MF_SA_D3D11_SHARED_WITHOUT_MUTEX, FALSE);
    attrs->SetUINT32(MF_SA_BUFFERS_PER_SAMPLE, 1);

    com_ptr<IMFMediaType> video_type{};
    REQUIRE(MFCreateMediaType(video_type.put()) == S_OK);
    video_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    video_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    video_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Unknown);
    MFSetAttributeSize(video_type.get(), MF_MT_FRAME_SIZE, 256, 256);

    // Call IMFVideoSampleAllocator::InitializeSampleAllocatorEx.
    if (auto hr = allocator->InitializeSampleAllocatorEx(5, 5, attrs.get(), video_type.get()); FAILED(hr))
        FAIL(hr);

    com_ptr<IMFSample> sample{};
    REQUIRE(allocator->AllocateSample(sample.put()) == S_OK);
    DWORD count{};
    sample->GetBufferCount(&count);
    REQUIRE(count == 1);
    com_ptr<IMFMediaBuffer> buffer{};
    REQUIRE(sample->GetBufferByIndex(0, buffer.put()) == S_OK);

    com_ptr<IMFDXGIBuffer> dxgi{};
    REQUIRE(buffer->QueryInterface(dxgi.put()) == S_OK);
    com_ptr<ID3D11Texture2D> texture{};
    REQUIRE(dxgi->GetResource(IID_PPV_ARGS(texture.put())) == S_OK);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    REQUIRE(desc.Format == DXGI_FORMAT_B8G8R8X8_UNORM);
    REQUIRE(desc.Width == 256);
    REQUIRE(desc.Height == 256);
    REQUIRE(desc.Usage == D3D11_USAGE_DEFAULT);
    REQUIRE((desc.BindFlags & D3D11_BIND_RENDER_TARGET));
    REQUIRE((desc.BindFlags & D3D11_BIND_SHADER_RESOURCE));
    REQUIRE_FALSE((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED)); // both flags are mutual exclusive
    REQUIRE((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX));
}

// For a Media Foundation transform (MFT), this step occurs during the MFT_MESSAGE_SET_D3D_MANAGER event.
TEST_CASE_METHOD(dxva_test_case, "DirectX Surface Buffer(MF_SA_D3D11_SHARED)") {
    // Call MFCreateVideoSampleAllocatorEx to create the allocator object and get a pointer to the IMFVideoSampleAllocatorEx interface.
    com_ptr<IMFVideoSampleAllocatorEx> allocator{};
    REQUIRE(MFCreateVideoSampleAllocatorEx(IID_IMFVideoSampleAllocatorEx, reinterpret_cast<void**>(allocator.put())) ==
            S_OK);
    // Call IMFVideoSampleAllocator::SetDirectXManager on the allocator to set the IMFDXGIDeviceManager pointer on the allocator.
    REQUIRE(allocator->SetDirectXManager(dxgi_manager.get()) == S_OK);

    // Call MFCreateAttributes to get a pointer to the IMFAttributes interface.
    com_ptr<IMFAttributes> attrs{};
    REQUIRE(MFCreateAttributes(attrs.put(), 5) == S_OK);
    // Set the MF_SA_D3D11_USAGE and MF_SA_D3D11_BINDFLAGS attributes.
    // todo: D3D11_USAGE_DEFAULT
    attrs->SetUINT32(MF_SA_D3D11_USAGE, D3D11_USAGE_DEFAULT);
    // todo: D3D11_BIND_DECODER, D3D11_BIND_VIDEO_ENCODER
    attrs->SetUINT32(MF_SA_D3D11_BINDFLAGS, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    attrs->SetUINT32(MF_SA_D3D11_SHARED, FALSE);
    attrs->SetUINT32(MF_SA_D3D11_SHARED_WITHOUT_MUTEX, TRUE);
    attrs->SetUINT32(MF_SA_BUFFERS_PER_SAMPLE, 1);

    com_ptr<IMFMediaType> video_type{};
    REQUIRE(MFCreateMediaType(video_type.put()) == S_OK);
    video_type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    video_type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    video_type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Unknown);
    MFSetAttributeSize(video_type.get(), MF_MT_FRAME_SIZE, 256, 256);

    // Call IMFVideoSampleAllocator::InitializeSampleAllocatorEx.
    if (auto hr = allocator->InitializeSampleAllocatorEx(5, 5, attrs.get(), video_type.get()); FAILED(hr))
        FAIL(hr);

    com_ptr<IMFSample> sample{};
    REQUIRE(allocator->AllocateSample(sample.put()) == S_OK);
    DWORD count{};
    sample->GetBufferCount(&count);
    REQUIRE(count == 1);
    com_ptr<IMFMediaBuffer> buffer{};
    REQUIRE(sample->GetBufferByIndex(0, buffer.put()) == S_OK);

    com_ptr<IMFDXGIBuffer> dxgi{};
    REQUIRE(buffer->QueryInterface(dxgi.put()) == S_OK);
    com_ptr<ID3D11Texture2D> texture{};
    REQUIRE(dxgi->GetResource(IID_PPV_ARGS(texture.put())) == S_OK);
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    REQUIRE((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED)); // both flags are mutual exclusive
    REQUIRE_FALSE((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX));
}

struct dxva_tex2d_test_case : public dx11_test_case {
    HRESULT make_texture(ID3D11Texture2D** texture) {
        D3D11_TEXTURE2D_DESC desc;
        desc.Width = 256;
        desc.Height = 256;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;                     // todo: use D3D11_CPU_ACCESS_WRITE?
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED; // todo: use D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX?
        return device->CreateTexture2D(&desc, nullptr, texture);
    }

    static HRESULT make_texture_surface(ID3D11Texture2D* tex2d, IMFSample** ptr) {
        // Create a DXGI media buffer by calling the MFCreateDXGISurfaceBuffer function.
        // Pass in the ID3D11Texture2D pointer and the offset for each element in the texture array.
        // The function returns an IMFMediaBuffer pointer.
        com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d, 0, false, buffer.put()); FAILED(hr))
            return hr;

        // Create an empty media sample by calling the MFCreateVideoSampleFromSurface function.
        // Set the pUnkSurface parameter equal to NULL. The function returns an IMFSample pointer.
        com_ptr<IMFSample> sample{};
        if (auto hr = MFCreateVideoSampleFromSurface(nullptr, sample.put()); FAILED(hr))
            return hr;

        // Call IMFSample::AddBuffer to add the media buffer to the sample.
        const HRESULT hr = sample->AddBuffer(buffer.get());
        if (SUCCEEDED(hr)) {
            *ptr = sample.get();
            sample->AddRef();
        }
        return hr;
    }
};

// @see https://docs.microsoft.com/en-us/windows/win32/api/mfapi/nf-mfapi-mfcreatedxgisurfacebuffer
TEST_CASE_METHOD(dxva_tex2d_test_case, "MFCreateDXGISurfaceBuffer") {
    com_ptr<ID3D11Texture2D> tex2d{};
    if (auto hr = make_texture(tex2d.put()); FAILED(hr))
        FAIL(hr);

    com_ptr<IMFMediaBuffer> buffer{};
    if (auto hr = MFCreateDXGISurfaceBuffer(IID_ID3D11Texture2D, tex2d.get(), 0, false, buffer.put()); FAILED(hr))
        FAIL(hr);

    SECTION("IMFDXGIBuffer") {
        com_ptr<IMFDXGIBuffer> dxgi{};
        REQUIRE(buffer->QueryInterface(dxgi.put()) == S_OK);
        com_ptr<ID3D11Texture2D> texture{};
        REQUIRE(dxgi->GetResource(IID_PPV_ARGS(texture.put())) == S_OK);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        REQUIRE(desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM);
    }
    SECTION("IMF2DBuffer2") {
        com_ptr<IMF2DBuffer2> buf2d{};
        REQUIRE(buffer->QueryInterface(buf2d.put()) == S_OK);
    }
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/directx-surface-buffer
    SECTION("IDirect3DSurface9") {
        com_ptr<IDirect3DSurface9> surface{};
        REQUIRE(MFGetService(buffer.get(), MR_BUFFER_SERVICE, IID_PPV_ARGS(surface.put())) == E_NOINTERFACE);
    }
}

TEST_CASE_METHOD(dxva_tex2d_test_case, "ID3D11Texture2D(SHARED)") {
    com_ptr<ID3D11Texture2D> tex2d{};
    if (auto hr = make_texture(tex2d.put()); FAILED(hr))
        FAIL(hr);

    com_ptr<IDXGIResource> dxgi = tex2d.as<IDXGIResource>();
    REQUIRE(dxgi);

    HANDLE handle{};
    REQUIRE(dxgi->GetSharedHandle(&handle) == S_OK);
}

TEST_CASE_METHOD(dxva_tex2d_test_case, "IMFSample from ID3D11Texture2D") {
    com_ptr<ID3D11Texture2D> tex2d{};
    if (auto hr = make_texture(tex2d.put()); FAILED(hr))
        FAIL(hr);

    com_ptr<IMFSample> sample{};
    REQUIRE(make_texture_surface(tex2d.get(), sample.put()) == S_OK);

    com_ptr<IMFMediaBuffer> buffer{};
    REQUIRE(sample->GetBufferByIndex(0, buffer.put()) == S_OK);
}
