#include <catch2/catch.hpp>

#include <d3d11_4.h>
#include <d3d9.h>
#include <dxgi1_2.h>
#include <dxva2api.h>
#include <winrt/base.h>

#include <spdlog/spdlog.h>

using winrt::com_ptr;

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
struct dxva_test_case {
    D3D_FEATURE_LEVEL level{};
    com_ptr<ID3D11Device> device{};
    com_ptr<ID3D11DeviceContext> context{};

  public:
    dxva_test_case() {
        D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1};
        auto hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL,
                                    D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, 2,
                                    D3D11_SDK_VERSION, device.put(), &level, context.put());
        if (FAILED(hr)) {
            spdlog::error("{}: {:#08x}", "D3D11CreateDevice", hr);
            FAIL(hr);
        }
    }
};

TEST_CASE_METHOD(dxva_test_case, "ID3D11Device FeatureLevel", "[dxva]") {
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

// https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-legacy-formats
// https://docs.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes
// https://docs.microsoft.com/en-us/windows/win32/medfound/video-subtype-guids#uncompressed-rgb-formats
