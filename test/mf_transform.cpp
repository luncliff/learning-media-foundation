#include "mf_transform.hpp"

#include <codecapi.h>
#include <d3d11_4.h>
#include <d3d9.h>
#include <dxva2api.h>
#include <evr.h>
#include <mediaobj.h>
#include <mmdeviceapi.h>
#include <spdlog/spdlog.h>
#include <wmcodecdsp.h>

winrt::com_ptr<IMFMediaType> make_video_type(const GUID& subtype) noexcept(false) {
    winrt::com_ptr<IMFMediaType> output{};
    if (auto hr = MFCreateMediaType(output.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = output->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
        winrt::throw_hresult(hr);
    return output;
}

void mf_transform_info_t::from(IMFTransform* transform) noexcept(false) {
    if (auto hr = transform->GetStreamCount(&num_input, &num_output); FAILED(hr))
        winrt::throw_hresult(hr);
    switch (auto hr = transform->GetStreamIDs(1, input_stream_ids, 1, output_stream_ids)) {
    case S_OK:
    case E_NOTIMPL:
        break; // some transform might not implement this.
    default:
        spdlog::error("{}: {:#08x} {}", "GetStreamIDs", hr, winrt::to_string(winrt::hresult_error{hr}.message()));
    }
    // some transforms require the I/O type configured
    if (auto hr = transform->GetInputStreamInfo(input_stream_ids[0], &input_info); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = transform->GetOutputStreamInfo(output_stream_ids[0], &output_info); FAILED(hr))
        winrt::throw_hresult(hr);
}

bool mf_transform_info_t::output_provide_sample() const noexcept {
    bool flag0 = output_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES;
    bool flag1 = output_info.dwFlags & MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES;
    return flag0 || flag1;
}

h264_decoder_t::h264_decoder_t(const GUID& clsid) noexcept(false) {
    winrt::com_ptr<IUnknown> unknown{};
    if (auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    configure_acceleration(transform.get());
}

h264_decoder_t::h264_decoder_t() noexcept(false) : h264_decoder_t{CLSID_CMSH264DecoderMFT} {
}

bool h264_decoder_t::support(IMFMediaType* source_type) const noexcept {
    GUID subtype{};
    if FAILED (source_type->GetGUID(MF_MT_SUBTYPE, &subtype))
        return false;
    return IsEqualGUID(subtype, MFVideoFormat_H264);
}

void h264_decoder_t::configure_acceleration(IMFTransform* transform) {
    winrt::com_ptr<IMFAttributes> attrs{};
    if (auto hr = transform->GetAttributes(attrs.put()); FAILED(hr))
        return spdlog::error("{}: {:#08x}", "Failed to get IMFAttributes of the IMFTransform", hr);
    if (auto hr = attrs->SetUINT32(CODECAPI_AVDecVideoAcceleration_H264, TRUE); FAILED(hr))
        spdlog::error("{}: {:#08x}", "CODECAPI_AVDecVideoAcceleration_H264", hr);
    if (auto hr = attrs->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE); FAILED(hr))
        spdlog::error("{}: {:#08x}", "CODECAPI_AVLowLatencyMode", hr);
    if (auto hr = attrs->SetUINT32(CODECAPI_AVDecNumWorkerThreads, 1); FAILED(hr))
        spdlog::error("{}: {:#08x}", "CODECAPI_AVDecNumWorkerThreads", hr);
}

color_converter_t::color_converter_t(const GUID& clsid) noexcept(false) {
    winrt::com_ptr<IUnknown> unknown{};
    if (auto hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    winrt::check_hresult(transform->QueryInterface(props.put()));
    winrt::check_hresult(transform->QueryInterface(media_object.put()));
}

color_converter_t::color_converter_t() noexcept(false) : color_converter_t{CLSID_CColorConvertDMO} {
}

sample_cropper_t::sample_cropper_t() noexcept(false) {
    winrt::com_ptr<IUnknown> unknown{};
    if (auto hr = CoCreateInstance(CLSID_CResizerDMO, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put())); FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    winrt::check_hresult(transform->QueryInterface(props0.put()));
}

HRESULT sample_cropper_t::crop(IMFMediaType* type, const RECT& region) noexcept {
    constexpr auto istream = 0, ostream = 0;
    try {
        if (auto hr = transform->SetInputType(istream, type, 0); FAILED(hr))
            return hr;
        uint32_t w = region.right - region.left;
        uint32_t h = region.bottom - region.top;
        if (auto hr = props0->SetClipRegion(region.left, region.top, w, h); FAILED(hr))
            return hr;
        GUID subtype{};
        if (auto hr = type->GetGUID(MF_MT_SUBTYPE, &subtype); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFMediaType> output = make_video_type(subtype);
        MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, w, h);
        return transform->SetOutputType(ostream, output.get(), 0);
    } catch (const winrt::hresult_error& err) {
        spdlog::error("{}: {:#08x} {}", __func__, err.code(), winrt::to_string(err.message()));
        return err.code();
    }
}

HRESULT sample_cropper_t::get_crop_region(RECT& src, RECT& dst) noexcept(false) {
    return props0->GetFullCropRegion(&src.left, &src.top, &src.right, &src.bottom, //
                                     &dst.left, &dst.top, &dst.right, &dst.bottom);
}

sample_processor_t::sample_processor_t() noexcept(false) {
    winrt::com_ptr<IUnknown> unknown{};
    if (auto hr = CoCreateInstance(CLSID_VideoProcessorMFT, nullptr, CLSCTX_ALL, IID_PPV_ARGS(unknown.put()));
        FAILED(hr))
        winrt::throw_hresult(hr);
    if (auto hr = unknown->QueryInterface(transform.put()); FAILED(hr))
        winrt::throw_hresult(hr);
    winrt::check_hresult(transform->QueryInterface(control.put()));
    winrt::check_hresult(transform->QueryInterface(realtime.put()));
}

HRESULT sample_processor_t::set_type(IMFMediaType* input, IMFMediaType* output) noexcept {
    constexpr auto istream = 0, ostream = 0;
    if (auto hr = transform->SetInputType(istream, input, 0); FAILED(hr))
        return hr;
    return transform->SetOutputType(ostream, output, 0);
}

HRESULT sample_processor_t::set_size(const RECT& rect) noexcept {
    RECT* ptr = const_cast<RECT*>(&rect);
    if (auto hr = control->SetSourceRectangle(ptr); FAILED(hr))
        return hr;
    return control->SetDestinationRectangle(ptr);
}

HRESULT sample_processor_t::set_scale(IMFMediaType* input, uint32_t width, uint32_t height) noexcept {
    constexpr auto istream = 0, ostream = 0;
    if (auto hr = transform->SetInputType(istream, input, 0); FAILED(hr))
        return hr;
    RECT region{0, 0, width, height};
    if (auto hr = control->SetDestinationRectangle(&region); FAILED(hr))
        return hr;
    try {
        GUID subtype{};
        if (auto hr = input->GetGUID(MF_MT_SUBTYPE, &subtype); FAILED(hr))
            return hr;
        winrt::com_ptr<IMFMediaType> output = make_video_type(subtype);
        MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, width, height);
        return transform->SetOutputType(ostream, output.get(), 0);
    } catch (const winrt::hresult_error& err) {
        spdlog::error("{}: {:#08x} {}", __func__, err.code(), winrt::to_string(err.message()));
        return err.code();
    }
}

HRESULT sample_processor_t::set_mirror_rotation(MF_VIDEO_PROCESSOR_MIRROR mirror,
                                                MF_VIDEO_PROCESSOR_ROTATION rotation) noexcept {
    if (auto hr = control->SetMirror(MF_VIDEO_PROCESSOR_MIRROR::MIRROR_VERTICAL); FAILED(hr))
        return hr;
    return control->SetRotation(MF_VIDEO_PROCESSOR_ROTATION::ROTATION_NORMAL);
}
