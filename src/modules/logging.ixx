module;

#if defined(__clang__)
#define FMT_CONSTEVAL
#endif

#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <spdlog/details/log_msg.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/formatter.h>
#include <spdlog/pattern_formatter.h>

#include <csignal>
#include <memory>
#include <mutex>
#include <string>

#include <comdef.h>
#include <processthreadsapi.h>
#include <windows.h>
#include <codecvt>
#include <locale>

export module logging;

namespace spdlog::sinks
{
    template <typename Mutex> class error_proxy_sink : public base_sink<Mutex>
    {
       private:
        using BaseSink = base_sink<Mutex>;

        std::shared_ptr<sink> sink_;

       public:
        explicit error_proxy_sink(std::shared_ptr<sink> sink) : sink_(sink) {}

        error_proxy_sink(const error_proxy_sink&) = delete;
        error_proxy_sink& operator=(const error_proxy_sink&) = delete;

       protected:
        void sink_it_(const spdlog::details::log_msg& msg) override
        {
            if (sink_->should_log(msg.level)) {
                sink_->log(msg);
            }
            if (spdlog::level::err == msg.level) {
                std::raise(SIGINT);
            }
        }

        void flush_() override { sink_->flush(); }

        void set_pattern_(const std::string& pattern) override
        {
            set_formatter_(spdlog::details::make_unique<spdlog::pattern_formatter>(pattern));
        }

        void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter) override
        {
            BaseSink::formatter_ = std::move(sink_formatter);
            sink_->set_formatter(BaseSink::formatter_->clone());
        }
    };

    using error_proxy_sink_mt = error_proxy_sink<std::mutex>;
    using error_proxy_sink_st = error_proxy_sink<spdlog::details::null_mutex>;
}

export void setupLogging();
