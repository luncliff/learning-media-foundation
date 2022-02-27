#include <catch2/catch.hpp>

// clang-format off
#include <Windows.h>
#include <d3d11_4.h>
#include <d3d9.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windowsx.h>

#include <codecapi.h>
#include <dxva2api.h>
#include <mediaobj.h>
#include <wmcodecdsp.h>
// clang-format on
#include <winrt/Windows.Foundation.h>

using winrt::com_ptr;

struct video_transform_test_case {

    static HRESULT make_video_type(IMFMediaType** ptr, const GUID& subtype) noexcept {
        com_ptr<IMFMediaType> type{};
        if (auto hr = MFCreateMediaType(type.put()); FAILED(hr))
            return hr;
        if (auto hr = type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video); FAILED(hr))
            return hr;
        if (auto hr = type->SetGUID(MF_MT_SUBTYPE, subtype); FAILED(hr))
            return hr;
        *ptr = type.get();
        type->AddRef();
        return S_OK;
    }

    static com_ptr<IMFMediaType> make_output(com_ptr<IMFMediaType> input, const GUID& subtype) {
        com_ptr<IMFMediaType> output{};
        REQUIRE(make_video_type(output.put(), subtype) == S_OK);
        if (subtype == MFVideoFormat_RGB32)
            REQUIRE(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Unknown) == S_OK);
        UINT32 w = 0, h = 0;
        REQUIRE(MFGetAttributeSize(input.get(), MF_MT_FRAME_SIZE, &w, &h) == S_OK);
        REQUIRE(MFSetAttributeSize(output.get(), MF_MT_FRAME_SIZE, w, h) == S_OK);
        UINT32 num = 0, denom = 1;
        REQUIRE(MFGetAttributeRatio(input.get(), MF_MT_FRAME_RATE, &num, &denom) == S_OK);
        REQUIRE(MFSetAttributeSize(output.get(), MF_MT_FRAME_RATE, num, denom) == S_OK);
        return output;
    };

    static HRESULT create_single_buffer_sample(IMFSample** output, DWORD bufsz) {
        if (auto hr = MFCreateSample(output); FAILED(hr))
            return hr;
        com_ptr<IMFMediaBuffer> buffer{};
        if (auto hr = MFCreateMemoryBuffer(bufsz, buffer.put()); FAILED(hr))
            return hr;
        // GetMaxLength will be length of the available memory location
        // GetCurrentLength will be 0
        IMFSample* sample = *output;
        return sample->AddBuffer(buffer.get());
    }

    static HRESULT get_transform_output(IMFTransform* transform, DWORD stream_id, //
                                        IMFSample** sample, GUID& subtype, BOOL& flushed) {
        MFT_OUTPUT_STREAM_INFO stream_info{};
        if (auto hr = transform->GetOutputStreamInfo(stream_id, &stream_info); FAILED(hr))
            return hr;

        flushed = FALSE;
        *sample = nullptr;

        MFT_OUTPUT_DATA_BUFFER output{};
        if ((stream_info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
            if (auto hr = create_single_buffer_sample(sample, stream_info.cbSize); FAILED(hr))
                return hr;
            output.pSample = *sample;
        }
        DWORD status = 0;
        HRESULT const result = transform->ProcessOutput(0, 1, &output, &status);
        if (result == S_OK) {
            *sample = output.pSample;
            return S_OK;
        }
        // see https://docs.microsoft.com/en-us/windows/win32/medfound/handling-stream-changes
        if (result == MF_E_TRANSFORM_STREAM_CHANGE) {
            com_ptr<IMFMediaType> changed_output_type{};
            if (output.dwStatus != MFT_OUTPUT_DATA_BUFFER_FORMAT_CHANGE) {
                // todo: add more works for this case
                return E_NOTIMPL;
            }
            // query the output type and its subtype
            if (auto hr = transform->GetOutputAvailableType(stream_id, 0, changed_output_type.put()); FAILED(hr))
                return hr;
            // check new output media type
            if (auto hr = changed_output_type->GetGUID(MF_MT_SUBTYPE, &subtype); FAILED(hr))
                return hr;

            if (auto hr = transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL); FAILED(hr))
                return hr;
            flushed = TRUE;
            return S_OK;
        }
        // MF_E_TRANSFORM_NEED_MORE_INPUT: not an error condition but it means the allocated output sample is empty.
        return result;
    }

    static void consume(com_ptr<IMFSourceReader> source_reader, com_ptr<IMFTransform> transform, //
                        DWORD istream, DWORD ostream) {
        bool input_available = true;
        while (input_available) {
            DWORD stream_index{};
            DWORD sample_flags{};
            LONGLONG sample_timestamp = 0; // unit 100-nanosecond
            com_ptr<IMFSample> input_sample{};
            if (auto hr = source_reader->ReadSample(istream, 0, &stream_index, &sample_flags, &sample_timestamp,
                                                    input_sample.put());
                FAILED(hr)) {
                CAPTURE(sample_flags);
                FAIL(hr);
            }
            if (sample_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                input_available = false;
                continue;
            }
            // probably MF_SOURCE_READERF_STREAMTICK
            if (input_sample == nullptr)
                continue;
            input_sample->SetSampleTime(sample_timestamp);
            switch (auto hr = transform->ProcessInput(istream, input_sample.get(), 0)) {
            case S_OK: // MF_E_TRANSFORM_TYPE_NOT_SET, MF_E_NO_SAMPLE_DURATION, MF_E_NO_SAMPLE_TIMESTAMP
                break;
            case MF_E_NOTACCEPTING:
            case MF_E_UNSUPPORTED_D3D_TYPE:
            case E_INVALIDARG:
            default:
                FAIL(hr);
            }
            while (true) {
                BOOL flushed = FALSE;
                com_ptr<IMFSample> sample{};
                GUID subtype{};
                auto hr = get_transform_output(transform.get(), ostream, sample.put(), subtype, flushed);
                if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                    break;
                if (FAILED(hr))
                    FAIL(hr);
            }
        }
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL) == S_OK);
        REQUIRE(transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL) == S_OK);
        // fetch remaining output in the transform
        while (true) {
            BOOL flushed = FALSE;
            com_ptr<IMFSample> sample{};
            GUID subtype{};
            auto hr = get_transform_output(transform.get(), ostream, sample.put(), subtype, flushed);
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
                break;
            if (FAILED(hr))
                FAIL(hr);
            // processed output
        }
    }
};