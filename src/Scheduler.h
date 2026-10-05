#pragma once

#include "ILuauHost.h"
#include "lua.h"
#include "uv.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Luwow::Task {
    using ILuauHost = Luwow::Engine::ILuauHost;

    class Scheduler {
        public:
            // In parallel mode the loop runs on its own thread, other threads hand it work through a queue
            Scheduler(ILuauHost* host, lua_State* mainThread, bool parallel);
            ~Scheduler();

            int spawn(lua_State* L);
            int defer(lua_State* L);
            int delay(lua_State* L, double seconds);
            int wait(lua_State* L, double seconds);
            int cancel(lua_State* L, lua_State* targetThread);

            // Resumes the thread with true once the socket is readable, or false if it can't be watched.
            void watch(lua_State* thread, uv_os_sock_t socket);

            // Cancels the thread's pending watch without resuming it, returns false if there was none.
            // Once this returns the socket isn't polled anymore, so it can be closed.
            bool unwatch(lua_State* thread);

            // Runs a fresh thread until it finishes, returning its status, LUA_YIELD if nothing is left to resume it
            int runThread(lua_State* thread);

            // Resumes a thread with the `nargs` values on top of its stack right away.
            // Errors are reported. Returns the resume status.
            int resumeNow(lua_State* thread, int nargs);

            // Resumes a thread with the `nargs` values on top of its stack after seconds.
            void resumeLater(lua_State* thread, int nargs, double seconds);

            // Waits until a yielded thread finishes, returns false if nothing is left to resume it.
            // Every level of the state lock the caller holds is released while waiting on another thread.
            bool waitForThread(lua_State* thread);

            // Runs the loop until no tasks are left, in parallel mode only after stop was requested
            void run();
            void stop();
        private:
            ILuauHost* host;
            lua_State* mainThread;
            bool parallel;

            uv_loop_t loop;
            uv_async_t async; // Wakes the loop thread for queued work, parallel mode only
            bool asyncClosed = false;
            std::atomic<std::thread::id> loopThread;

            // Guards everything below, which both the loop thread and Luau callers use
            std::mutex mutex;
            std::vector<std::function<void()>> commands;
            bool acceptingCommands = true;

            // Maps a suspended lua_State (thread) to its active libuv timer
            std::unordered_map<lua_State*, uv_timer_t*> pendingTimers;

            // Maps a suspended lua_State (thread) to the libuv poll watching its socket
            std::unordered_map<lua_State*, uv_poll_t*> pendingWatches;

            // Counts resumed timers, so a thread waiting for a script can tell something ran
            std::condition_variable resumed;
            uint64_t resumeCount = 0;

            bool isLoopThread() const;
            void post(std::function<void()> command);
            void runCommands();
            void notifyResumed();
            void addTimer(lua_State* thread, int threadRef, int nargs, bool isWait, double seconds);

            static void timerCb(uv_timer_t* handle);
            static void pollCb(uv_poll_t* handle, int status, int events);
            static void asyncCb(uv_async_t* handle);
            static void closeTimer(uv_timer_t* timer);
            static void closePoll(uv_poll_t* poll);

            // Resumes a watching thread once its socket is ready, false if the watch was cancelled meanwhile
            bool finishWatch(uv_poll_t* poll);
    };

    struct TaskPollData {
        Scheduler* scheduler;
        lua_State* thread;
        int threadRef;
        bool cancelled;
        bool initialized;
    };

    struct TaskTimerData {
        Scheduler* scheduler;
        lua_State* thread;
        int threadRef;
        int nargs;
        uint64_t startNs;
        bool isWait;
        bool cancelled;
    };
} // namespace Luwow::Task
