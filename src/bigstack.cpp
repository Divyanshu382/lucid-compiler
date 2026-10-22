// ============================================================================
// bigstack.cpp — see bigstack.hpp.
// ============================================================================
#include "bigstack.hpp"

#include <exception>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <pthread.h>
#endif

namespace lucid {

namespace {

constexpr size_t kStackBytes = 256u * 1024u * 1024u;

struct Job {
    const std::function<void()>* fn;
    std::exception_ptr error;
};

void runJob(Job* job) {
    try { (*job->fn)(); }
    catch (...) { job->error = std::current_exception(); }
}

#ifdef _WIN32
DWORD WINAPI threadMain(LPVOID arg) { runJob(static_cast<Job*>(arg)); return 0; }
#else
void* threadMain(void* arg) { runJob(static_cast<Job*>(arg)); return nullptr; }
#endif

} // namespace

void runOnLargeStack(const std::function<void()>& fn) {
    Job job{&fn, nullptr};
    bool started = false;
#ifdef _WIN32
    HANDLE t = CreateThread(nullptr, kStackBytes, threadMain, &job,
                            STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (t) {
        started = true;
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
#else
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStackBytes);
    pthread_t t;
    if (pthread_create(&t, &attr, threadMain, &job) == 0) {
        started = true;
        pthread_join(t, nullptr);
    }
    pthread_attr_destroy(&attr);
#endif
    if (!started) fn();                       // could not create a thread: run here
    if (job.error) std::rethrow_exception(job.error);
}

} // namespace lucid
