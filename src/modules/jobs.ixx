module;

#include <functional>

export module jobs;

// JobSystem — thin owner around marl's scheduler. Initialised once by
// Application and bound to the main thread so the rest of the codebase can
// fire-and-forget tasks via JobSystem::schedule(). Future passes will wire
// physics step, animation sampling, and asset cooks through this layer.
//
// As with audio/physics, the implementation TU holds the marl includes so the
// module interface stays slim.
export class JobSystem
{
   public:
    JobSystem();
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    bool isReady() const { return ready; }

    // Schedule fn to run on the worker pool. The function is std::function so
    // captures work; lifetime is fire-and-forget. For barrier-style waits use
    // a marl::WaitGroup directly inside the function or a future helper.
    void schedule(std::function<void()> fn);

    // Number of worker threads, useful for capacity-sensitive callers
    // (e.g. parallel-for chunk sizing).
    int workerCount() const { return workers; }

   private:
    void* impl = nullptr;
    int workers = 0;
    bool ready = false;
};
