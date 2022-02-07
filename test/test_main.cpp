#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <catch2/catch_reporter_sonarqube.hpp>

#include <experimental/coroutine>
#include <filesystem>
#include <mfapi.h>
// https://docs.microsoft.com/en-us/windows/win32/sysinfo/getting-the-system-version
#include <DispatcherQueue.h>
#include <VersionHelpers.h>
#include <pplawait.h>
#include <ppltasks.h>
#include <winrt/Windows.Foundation.h> // namespace winrt::Windows::Foundation
#include <winrt/Windows.System.h>     // namespace winrt::Windows::System
#include <winrt/base.h>

#include <gsl/gsl>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

fs::path get_asset_dir() noexcept {
#if defined(ASSET_DIR)
    fs::path asset_dir{ASSET_DIR};
    if (fs::exists(asset_dir))
        return asset_dir;
#endif
    return fs::current_path();
}

bool has_env(const char* key) noexcept {
    size_t len = 0;
    char buf[40]{};
    if (auto ec = getenv_s(&len, buf, key); ec != 0)
        return false;
    std::string_view value{buf, len};
    return value.empty() == false;
}

auto make_logger(const char* name, FILE* fout) noexcept(false) {
    using mutex_t = spdlog::details::console_nullmutex;
    using sink_t = spdlog::sinks::stdout_sink_base<mutex_t>;
    return std::make_unique<spdlog::logger>(name, std::make_shared<sink_t>(fout));
}

class test_suite_context_t final {
  public:
    test_suite_context_t() {
        winrt::init_apartment();
        winrt::check_hresult(MFStartup(MF_VERSION));
    }
    ~test_suite_context_t() {
        MFShutdown();
        winrt::uninit_apartment();
    }

    void use_environment(wchar_t* envp[]) {
        if (envp)
            spdlog::debug("envs:");
        for (wchar_t** ptr = envp; (*ptr) != nullptr; ++ptr)
            spdlog::debug(" - {}", winrt::to_string(std::wstring_view{*ptr}));
    }
};

test_suite_context_t context{};

int wmain(int argc, wchar_t* argv[], wchar_t* envp[]) {
    std::shared_ptr<spdlog::logger> logger = make_logger("test", stdout);
    logger->set_pattern("%T.%e [%L] %8t %v");
    logger->set_level(spdlog::level::level_enum::debug);
    spdlog::set_default_logger(logger);

    context.use_environment(envp);

    spdlog::info("C++/WinRT:");
    spdlog::info("  version: {:s}", CPPWINRT_VERSION); // WINRT_version
    spdlog::info("Windows Media Foundation:");
    spdlog::info("  SDK: {:X}", MF_SDK_VERSION);
    spdlog::info("  API: {:X}", MF_API_VERSION);

    Catch::Session session{};
    session.applyCommandLine(argc, argv);
    return session.run();
}

using std::experimental::coroutine_handle;
using winrt::Windows::Foundation::IAsyncAction;
using winrt::Windows::Foundation::IAsyncOperation;
using winrt::Windows::System::DispatcherQueue;
using winrt::Windows::System::DispatcherQueueController;

struct winrt_test_case {
    /// @see https://devblogs.microsoft.com/oldnewthing/20191223-00/?p=103255
    [[nodiscard]] auto resume_on_queue(winrt::Windows::System::DispatcherQueue dispatcher) {
        struct awaitable_t final {
            winrt::Windows::System::DispatcherQueue dispatcher;

            bool await_ready() const noexcept {
                return dispatcher == nullptr;
            }
            bool await_suspend(coroutine_handle<void> handle) noexcept {
                return dispatcher.TryEnqueue(handle);
            }
            constexpr void await_resume() const noexcept {
            }
        };
        return awaitable_t{dispatcher};
    }

    /// @throws winrt::hresult_error
    [[nodiscard]] auto query_thread_id(winrt::Windows::System::DispatcherQueue queue) noexcept(false)
        -> winrt::Windows::Foundation::IAsyncOperation<uint32_t> {
        //co_await winrt::resume_foreground(queue);
        co_await resume_on_queue(queue);
        co_return GetCurrentThreadId();
    }
};

TEST_CASE_METHOD(winrt_test_case, "DispatcherQueue::ShutdownQueueAsync", "[winrt]") {
    DispatcherQueueController controller = DispatcherQueueController::CreateOnDedicatedThread();
    DispatcherQueue worker_queue = controller.DispatcherQueue();
    REQUIRE(worker_queue);
    IAsyncAction operation = controller.ShutdownQueueAsync();
    REQUIRE_NOTHROW(operation.get());
}

/// @see https://gist.github.com/kennykerr/6490e1494449927147dc18616a5e601e
/// @todo use CreateOnDedicatedThread
TEST_CASE_METHOD(winrt_test_case, "DispatcherQueue::TryEnqueue", "[WinRT]") {
    DispatcherQueueController controller = winrt::Windows::System::DispatcherQueueController::CreateOnDedicatedThread();
    auto on_return = gsl::finally([controller]() {
        IAsyncAction operation = controller.ShutdownQueueAsync();
        operation.get();
    });
    DispatcherQueue queue = controller.DispatcherQueue();
    REQUIRE(queue);
    DWORD current = GetCurrentThreadId();
    DWORD dedicated = query_thread_id(queue).get();
    REQUIRE(current != dedicated);
}
