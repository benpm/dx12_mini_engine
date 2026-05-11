module;

#if defined(__clang__)
    #define FMT_CONSTEVAL
#endif

#include <marl/scheduler.h>
#include <marl/thread.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <thread>

module jobs;

struct JobsImpl
{
    marl::Scheduler::Config cfg;
    marl::Scheduler* scheduler = nullptr;
};

JobSystem::JobSystem()
{
    auto* state = new JobsImpl();
    state->cfg.setWorkerThreadCount(
        std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1)
    );
    state->scheduler = new marl::Scheduler(state->cfg);
    state->scheduler->bind();
    impl = state;
    workers = state->cfg.workerThread.count;
    ready = true;
    spdlog::info("JobSystem: marl scheduler bound (workers={})", workers);
}

JobSystem::~JobSystem()
{
    auto* state = static_cast<JobsImpl*>(impl);
    if (state) {
        if (state->scheduler) {
            state->scheduler->unbind();
            delete state->scheduler;
            state->scheduler = nullptr;
        }
        delete state;
        impl = nullptr;
    }
}

void JobSystem::schedule(std::function<void()> fn)
{
    if (!ready) {
        // Run synchronously if the scheduler isn't up — keeps callers correct.
        if (fn) {
            fn();
        }
        return;
    }
    marl::schedule(std::move(fn));
}
