/**
 * @see https://docs.microsoft.com/en-us/windows/apps/windows-app-sdk/set-up-your-development-environment
 */
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>

#include <DirectXTK/DirectXHelpers.h>
#include <DirectXTex.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

auto make_logger(const char* name, FILE* fout) noexcept(false) {
    using mutex_t = spdlog::details::console_nullmutex;
    using sink_t = spdlog::sinks::stdout_sink_base<mutex_t>;
    return std::make_shared<spdlog::logger>(name, std::make_shared<sink_t>(fout));
}

class application_t {
    D3D_FEATURE_LEVEL device_feature_level{};
    com_ptr<ID3D11Device> device{};
    com_ptr<ID3D11DeviceContext> context{};
    com_ptr<ID3D10Multithread> threading{};

  public:
    std::shared_ptr<spdlog::logger> logger = nullptr;

  public:
    application_t() {
        logger = make_logger("test", stdout);
        logger->set_pattern("%T.%e [%L] %8t %v");
        logger->set_level(spdlog::level::level_enum::debug);
        winrt::init_apartment();
    }
    ~application_t() {
    }
};

int wmain(int argc, wchar_t* argv[], wchar_t*[]) {
    application_t app{};
    spdlog::set_default_logger(app.logger);

    winrt::init_apartment();
    spdlog::info("C++/WinRT:");
    spdlog::info("  version: {:s}", CPPWINRT_VERSION); // WINRT_version

    Catch::Session session{};
    session.applyCommandLine(argc, argv);
    return session.run();
}
