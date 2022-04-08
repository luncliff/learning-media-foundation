
#include <catch2/catch.hpp>

// clang-format off
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.System.h>

#include <mfobjects.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h> // for IMFRealTimeClientEx
#include <synchapi.h>
// clang-format on
#include <spdlog/spdlog.h>

/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/work-queues
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/using-work-queues
/// @see https://docs.microsoft.com/en-us/windows/win32/medfound/media-foundation-work-queue-and-threading-improvements
struct mf_scheduler_t final {
    DWORD queue = 0;

  public:
    mf_scheduler_t() noexcept(false) {
        if (auto hr = MFAllocateWorkQueueEx(MF_STANDARD_WORKQUEUE, &queue); FAILED(hr))
            winrt::throw_hresult(hr);
    }
    ~mf_scheduler_t() {
        MFUnlockWorkQueue(queue);
    }

    winrt::com_ptr<IMFAsyncResult> put(IMFAsyncCallback* callback, LONG priority) noexcept(false) {
        winrt::com_ptr<IMFAsyncResult> result{};
        winrt::check_hresult(MFCreateAsyncResult(nullptr, callback, nullptr, result.put()));
        //winrt::check_hresult(MFPutWorkItemEx(queue, result.get()));
        winrt::check_hresult(MFPutWorkItemEx2(queue, priority, result.get()));
        return result;
    }
};

namespace std {
template <>
struct lock_guard<mf_scheduler_t> {
    mf_scheduler_t& ref;

  public:
    void lock() noexcept(false) {
        winrt::check_hresult(MFLockWorkQueue(ref.queue));
    }
    void unlock() noexcept(false) {
        winrt::check_hresult(MFUnlockWorkQueue(ref.queue));
    }
};
} // namespace std

struct mf_scheduler_test_case : public IMFAsyncCallback {
    mf_scheduler_t scheduler{};
    LONG ref_count = 1;
    HANDLE invoked = nullptr;

  public:
    mf_scheduler_test_case() {
        invoked = CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, SYNCHRONIZE | EVENT_ALL_ACCESS);
        ResetEvent(invoked);
    }
    ~mf_scheduler_test_case() {
        CloseHandle(invoked);
    }

  private:
    HRESULT __stdcall GetParameters(DWORD* flags, DWORD* queue) final {
        spdlog::debug("{}: flags {} queue {}", __func__, *flags, *queue);
        return S_OK;
    }

    HRESULT __stdcall Invoke(IMFAsyncResult* result) final {
        spdlog::debug("{}: {}", __func__, reinterpret_cast<void*>(result));

        winrt::com_ptr<IUnknown> state{};
        if (auto hr = result->GetState(state.put()); FAILED(hr))
            spdlog::error("{}: {:#08x}", "GetState", static_cast<uint32_t>(hr));

        SetEvent(invoked);
        return result->SetStatus(E_ABORT);
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppv) final {
        if (riid == IID_IUnknown)
            *ppv = this;
        else if (riid == IID_IMFAsyncCallback)
            *ppv = this;
        return *ppv ? S_OK : E_NOINTERFACE;
    }

    ULONG __stdcall AddRef() final {
        return InterlockedAdd(&ref_count, 1);
    }

    ULONG __stdcall Release() final {
        const auto count = InterlockedDecrement(&ref_count);
        if (count == 0)
            spdlog::warn("{}: {}", "bad count", count);
        return count;
    }
};

TEST_CASE_METHOD(mf_scheduler_test_case, "Simple work queue") {
    auto res = scheduler.put(this, 0);
    REQUIRE(WaitForSingleObjectEx(invoked, INFINITE, true) == WAIT_OBJECT_0);
    REQUIRE(res->GetStatus() == E_ABORT);
}
