#pragma once
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>

#include <winrt/Windows.Foundation.h>

struct mf_transform_info_t final {
    DWORD num_input = 0;
    DWORD num_output = 0;
    DWORD input_stream_ids[1]{};
    DWORD output_stream_ids[1]{};
    MFT_INPUT_STREAM_INFO input_info{};
    MFT_OUTPUT_STREAM_INFO output_info{};

  public:
    /// @todo check flags related to sample/buffer constraint
    void from(IMFTransform* transform) noexcept(false);

    /// @see MFT_OUTPUT_STREAM_PROVIDES_SAMPLES
    /// @see MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES
    [[nodiscard]] bool output_provide_sample() const noexcept;
};

/**
 * @brief `IMFTransform` owner for `MFVideoFormat_H264`
 * @todo Support `MFVideoFormat_H264_ES`, `MFVideoFormat_H264_HDCP`
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder
 * @see https://docs.microsoft.com/en-us/windows/win32/medfound/basic-mft-processing-model
 */
struct h264_decoder_t final {
    winrt::com_ptr<IMFTransform> transform{};

  public:
    explicit h264_decoder_t(const GUID& clsid) noexcept(false);
    h264_decoder_t() noexcept(false);

    [[nodiscard]] bool support(IMFMediaType* source_type) const noexcept;

  public:
    /// @see https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-decoder#transform-attributes
    static void configure_acceleration(IMFTransform* transform);
};

/// @see CLSID_CColorConvertDMO
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/colorconverter
/// @see Microsoft DirectX Media Object https://docs.microsoft.com/en-us/previous-versions/windows/desktop/api/mediaobj/nn-mediaobj-imediaobject
struct color_converter_t final {
    winrt::com_ptr<IMFTransform> transform{};
    winrt::com_ptr<IPropertyStore> props{};
    winrt::com_ptr<IMediaObject> media_object{};

  public:
    explicit color_converter_t(const GUID& clsid) noexcept(false);
    color_converter_t() noexcept(false);
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/videoresizer
struct sample_cropper_t final {
    winrt::com_ptr<IMFTransform> transform{};
    winrt::com_ptr<IWMResizerProps> props0{};

  public:
    sample_cropper_t() noexcept(false);

    [[nodiscard]] HRESULT crop(IMFMediaType* type, const RECT& region) noexcept;
    [[nodiscard]] HRESULT get_crop_region(RECT& src, RECT& dst) const noexcept;
};

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-work-queue-and-threading-improvements
struct sample_processor_t {
    winrt::com_ptr<IMFTransform> transform{};
    winrt::com_ptr<IMFVideoProcessorControl> control{};
    winrt::com_ptr<IMFRealTimeClientEx> realtime{};

  public:
    sample_processor_t() noexcept(false);

    [[nodiscard]] HRESULT set_type(IMFMediaType* input, IMFMediaType* output) noexcept;
    [[nodiscard]] HRESULT set_scale(IMFMediaType* input, uint32_t width, uint32_t height) noexcept;
    [[nodiscard]] HRESULT set_size(const RECT& rect) noexcept;
    [[nodiscard]] HRESULT set_color(const MFARGB& color) noexcept;
    [[nodiscard]] HRESULT set_mirror_rotation(MF_VIDEO_PROCESSOR_MIRROR mirror,
                                              MF_VIDEO_PROCESSOR_ROTATION rotation) noexcept;
};
