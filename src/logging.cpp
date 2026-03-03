module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <cstdio>
#include <windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

module logging;

void setupLogging()
{
    // Attach to parent console so stderr/stdout work when launched from a terminal
    if (AttachConsole(ATTACH_PARENT_PROCESS) != 0) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONERR$", "w", stderr);
        freopen_s(&dummy, "CONOUT$", "w", stdout);
    }

    // Logger setup
    auto stderrSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("log.txt", true);
    auto errSink = std::make_shared<spdlog::sinks::error_proxy_sink_mt>(stderrSink);
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->set_pattern("%R|%^%7l%$> %v");
    auto logger =
        std::make_shared<spdlog::logger>("logger", spdlog::sinks_init_list{ errSink, fileSink });
    logger->set_formatter(std::move(formatter));
    spdlog::set_default_logger(logger);
    spdlog::flush_on(spdlog::level::trace);
#ifdef _DEBUG
    spdlog::set_level(spdlog::level::trace);
    spdlog::info("Running in Debug Mode");
#else
    spdlog::set_level(spdlog::level::info);
    spdlog::info("Running in Release Mode");
#endif
}