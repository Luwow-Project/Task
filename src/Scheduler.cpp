#include "Scheduler.h"
#include "lualib.h"
#include "uv.h"

#include <cstdio>

namespace Luwow::Task {
    // Static helper functions

    static lua_State* coerceToThread(lua_State* L) {
        if (lua_isfunction(L, 1)) {
            lua_State* TL = lua_newthread(L);
            lua_pushvalue(L, 1);
            lua_xmove(L, TL, 1);
            lua_remove(L, 1);
            lua_insert(L, 1);
            return TL;
        }
        if (lua_isthread(L, 1)) {
            return lua_tothread(L, 1);
        }
        luaL_error(L, "function or thread expected");
        return nullptr;
    }

    static int moveArgs(lua_State* from, lua_State* to) {
        int nargs = lua_gettop(from) - 1;
        if (nargs > 0) {
            lua_xmove(from, to, nargs);
        }
        return nargs;
    }

    // Constructor and Destructor

    Scheduler::Scheduler(ILuauHost* host, lua_State* mainThread, bool parallel)
        : host(host), mainThread(mainThread), parallel(parallel) {
        uv_loop_init(&loop);

        if (parallel) {
            uv_async_init(&loop, &async, asyncCb);
            async.data = this;
        }
    }

    // The loop thread has ended and the state may be closed, so only the loop's handles are released
    Scheduler::~Scheduler() {
        runCommands();

        for (auto& pair : pendingTimers) {
            closeTimer(pair.second);
        }
        pendingTimers.clear();

        for (auto& pair : pendingWatches) {
            closePoll(pair.second);
        }
        pendingWatches.clear();

        if (parallel && !asyncClosed) {
            asyncClosed = true;
            uv_close((uv_handle_t*)&async, nullptr);
        }

        uv_run(&loop, UV_RUN_NOWAIT);
        uv_loop_close(&loop);
    }

    // Class methods

    int Scheduler::spawn(lua_State* L) {
        bool wasExistingThread = lua_isthread(L, 1);
        lua_State* TL = coerceToThread(L);

        if (wasExistingThread) {
            cancel(L, TL);
        }

        int nargs = moveArgs(L, TL);
        int status = lua_costatus(L, TL);

        if (status != LUA_COSUS) {
            luaL_error(L, "cannot resume coroutine");
        }

        int res = lua_resume(TL, L, nargs);
        if (res != LUA_OK && res != LUA_YIELD && res != LUA_BREAK) {
            lua_xmove(TL, L, 1);
            lua_error(L);
        }

        lua_settop(L, 1);
        return 1;
    }

    int Scheduler::defer(lua_State* L) {
        return delay(L, 0.0); // defer is just a delay with 0 seconds
    }

    int Scheduler::delay(lua_State* L, double seconds) {
        lua_State* TL = coerceToThread(L);
        int nargs = moveArgs(L, TL);

        lua_pushvalue(L, 1);
        int ref = lua_ref(L, -1);
        lua_pop(L, 1);

        addTimer(TL, ref, nargs, false, seconds);

        lua_settop(L, 1);
        return 1;
    }

    int Scheduler::wait(lua_State* L, double seconds) {
        lua_pushthread(L);
        lua_State* TL = lua_tothread(L, -1);
        int ref = lua_ref(L, -1);
        lua_pop(L, 1);

        addTimer(TL, ref, 0, true, seconds);

        return lua_yield(L, 0);
    }

    int Scheduler::cancel(lua_State* L, lua_State* targetThread) {
        uv_timer_t* timer = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pendingTimers.find(targetThread);
            if (it != pendingTimers.end()) {
                timer = it->second;
                static_cast<TaskTimerData*>(timer->data)->cancelled = true;
                pendingTimers.erase(it);
            }
        }

        if (timer) {
            lua_unref(mainThread, static_cast<TaskTimerData*>(timer->data)->threadRef);
            post([timer] { closeTimer(timer); });
        }
        return 0;
    }

    void Scheduler::watch(lua_State* thread, uv_os_sock_t socket) {
        lua_pushthread(thread);
        int ref = lua_ref(thread, -1);
        lua_pop(thread, 1);

        auto* poll = new uv_poll_t;
        poll->data = new TaskPollData { this, thread, ref, false, false };

        {
            std::lock_guard<std::mutex> lock(mutex);
            pendingWatches[thread] = poll;
        }

        post([this, poll, socket] {
            auto* data = static_cast<TaskPollData*>(poll->data);
            if (uv_poll_init_socket(&loop, poll, socket) == 0) {
                data->initialized = true;
                uv_poll_start(poll, UV_READABLE, pollCb);
                return;
            }

            // The requesting thread may not have yielded yet, so it can't be resumed with false here.
            // The watch is dropped instead, unless it was cancelled, then its close cleans up.
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (data->cancelled) return;
                pendingWatches.erase(data->thread);
            }
            fprintf(stderr, "[Task Scheduler] Could not watch a socket\n");
            {
                Luwow::Engine::StateLock stateLock(host);
                lua_unref(mainThread, data->threadRef);
            }
            delete data;
            delete poll;
        });
    }

    bool Scheduler::unwatch(lua_State* thread) {
        uv_poll_t* poll = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pendingWatches.find(thread);
            if (it == pendingWatches.end()) return false;

            poll = it->second;
            static_cast<TaskPollData*>(poll->data)->cancelled = true;
            pendingWatches.erase(it);
        }

        lua_unref(mainThread, static_cast<TaskPollData*>(poll->data)->threadRef);

        if (isLoopThread()) {
            runCommands();
            closePoll(poll);
            return true;
        }

        // The socket may only be closed once the loop stopped polling it, so wait for the loop thread.
        // It may need the state lock to finish a callback first, so every level of it is released meanwhile.
        bool done = false;
        post([this, poll, &done] {
            closePoll(poll);
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
            }
            resumed.notify_all();
        });

        Luwow::Engine::StateRelease release(host);
        std::unique_lock<std::mutex> lock(mutex);
        resumed.wait(lock, [&] { return done || !acceptingCommands; });
        return true;
    }

    void Scheduler::run() {
        loopThread = std::this_thread::get_id();
        uv_run(&loop, UV_RUN_DEFAULT);
    }

    // Closing the wake-up handle lets the loop end once its remaining timers have fired
    void Scheduler::stop() {
        if (!parallel) return;

        post([this] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                acceptingCommands = false;
            }
            if (!asyncClosed) {
                asyncClosed = true;
                uv_close((uv_handle_t*)&async, nullptr);
            }
            notifyResumed();
        });
    }

    int Scheduler::resumeNow(lua_State* thread, int nargs) {
        int status = lua_resume(thread, mainThread, nargs);
        if (status != LUA_OK && status != LUA_YIELD && status != LUA_BREAK) {
            if (lua_isstring(thread, -1)) {
                fprintf(stderr, "[Task Scheduler] Error in resumed thread: %s\n", lua_tostring(thread, -1));
            }
            lua_pop(thread, 1);
        }

        // A thread waiting for a script may be waiting on the one just resumed
        notifyResumed();
        return status;
    }

    void Scheduler::resumeLater(lua_State* thread, int nargs, double seconds) {
        lua_pushthread(thread);
        int ref = lua_ref(thread, -1);
        lua_pop(thread, 1);

        addTimer(thread, ref, nargs, false, seconds < 0 ? 0 : seconds);
    }

    int Scheduler::runThread(lua_State* thread) {
        int status = lua_resume(thread, nullptr, 0);
        if (status == LUA_YIELD && !waitForThread(thread)) return LUA_YIELD;
        return lua_status(thread);
    }

    bool Scheduler::waitForThread(lua_State* thread) {
        while (lua_status(thread) == LUA_YIELD) {
            if (isLoopThread()) {
                // The loop belongs to this thread, so it's driven from here
                int hasActiveHandles = uv_run(&loop, UV_RUN_ONCE);
                if (hasActiveHandles == 0 && lua_status(thread) == LUA_YIELD) return false;
                continue;
            }

            // Let the loop thread resume the thread, it needs the state lock to do so
            std::unique_lock<std::mutex> lock(mutex);
            if (!acceptingCommands || (pendingTimers.empty() && pendingWatches.empty() && commands.empty())) return false;

            uint64_t seen = resumeCount;
            lock.unlock();

            Luwow::Engine::StateRelease release(host);
            lock.lock();
            resumed.wait(lock, [&] { return resumeCount != seen; });
            lock.unlock();
        }
        return true;
    }

    // Private helpers

    bool Scheduler::isLoopThread() const {
        return !parallel || std::this_thread::get_id() == loopThread.load();
    }

    // Runs a command on the loop thread, in the order it was posted
    void Scheduler::post(std::function<void()> command) {
        if (isLoopThread()) {
            runCommands();
            command();
            return;
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!acceptingCommands) return;
            commands.push_back(std::move(command));
        }
        uv_async_send(&async);
    }

    void Scheduler::runCommands() {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex);
            batch.swap(commands);
        }
        for (auto& command : batch) command();
    }

    void Scheduler::notifyResumed() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++resumeCount;
        }
        resumed.notify_all();
    }

    void Scheduler::addTimer(lua_State* thread, int threadRef, int nargs, bool isWait, double seconds) {
        auto* timer = new uv_timer_t;
        timer->data = new TaskTimerData { this, thread, threadRef, nargs, uv_hrtime(), isWait, false };

        {
            std::lock_guard<std::mutex> lock(mutex);
            pendingTimers[thread] = timer;
        }

        uint64_t ms = (uint64_t)(seconds * 1000.0);
        post([this, timer, ms] {
            uv_timer_init(&loop, timer);
            uv_timer_start(timer, timerCb, ms, 0);
        });
    }

    // Callback class methods

    // The timer stays pending until its thread was resumed, so a thread waiting for a script
    // never sees an empty scheduler while a resume is still on its way
    void Scheduler::timerCb(uv_timer_t* handle) {
        auto* data = static_cast<TaskTimerData*>(handle->data);
        Scheduler* self = data->scheduler;

        {
            Luwow::Engine::StateLock stateLock(self->host);

            // Checked under the state lock, since cancel may run while waiting for it, then it queues the close
            {
                std::lock_guard<std::mutex> cancelLock(self->mutex);
                if (data->cancelled) return;
            }

            lua_State* TL = data->thread;
            int nargs = data->nargs;
            if (data->isWait) {
                double elapsed = (uv_hrtime() - data->startNs) / 1e9;
                lua_pushnumber(TL, elapsed);
                nargs = 1;
            }

            int res = lua_resume(TL, self->mainThread, nargs);
            if (res != LUA_OK && res != LUA_YIELD && res != LUA_BREAK) {
                if (lua_isstring(TL, -1)) {
                    fprintf(stderr, "[Task Scheduler] Error in %s: %s\n", data->isWait ? "wait" : "delay", lua_tostring(TL, -1));
                }
                lua_pop(TL, 1);
            }

            lua_unref(self->mainThread, data->threadRef);

            // The thread may have started a new timer while resuming, which then stays pending
            std::lock_guard<std::mutex> timersLock(self->mutex);
            auto it = self->pendingTimers.find(data->thread);
            if (it != self->pendingTimers.end() && it->second == handle) {
                self->pendingTimers.erase(it);
            }
        }

        self->notifyResumed();
        closeTimer(handle);
    }

    // Each watch fires once, so polling stops before the thread is resumed and the socket may be closed by it
    void Scheduler::pollCb(uv_poll_t* handle, int status, int events) {
        auto* data = static_cast<TaskPollData*>(handle->data);
        uv_poll_stop(handle);

        // A cancelled watch is closed by the unwatch that cancelled it
        if (data->scheduler->finishWatch(handle)) {
            closePoll(handle);
        }
    }

    bool Scheduler::finishWatch(uv_poll_t* poll) {
        auto* data = static_cast<TaskPollData*>(poll->data);

        {
            Luwow::Engine::StateLock stateLock(host);

            // Erased under the state lock, so a thread waiting for a script never sees an empty
            // scheduler while this resume is on its way
            {
                std::lock_guard<std::mutex> cancelLock(mutex);
                if (data->cancelled) return false;
                auto it = pendingWatches.find(data->thread);
                if (it != pendingWatches.end() && it->second == poll) {
                    pendingWatches.erase(it);
                }
            }

            lua_State* thread = data->thread;
            lua_pushboolean(thread, true);
            int res = lua_resume(thread, mainThread, 1);
            if (res != LUA_OK && res != LUA_YIELD && res != LUA_BREAK) {
                if (lua_isstring(thread, -1)) {
                    fprintf(stderr, "[Task Scheduler] Error in socket watcher: %s\n", lua_tostring(thread, -1));
                }
                lua_pop(thread, 1);
            }

            lua_unref(mainThread, data->threadRef);
        }

        notifyResumed();
        return true;
    }

    void Scheduler::asyncCb(uv_async_t* handle) {
        static_cast<Scheduler*>(handle->data)->runCommands();
    }

    void Scheduler::closeTimer(uv_timer_t* timer) {
        uv_close((uv_handle_t*)timer, [](uv_handle_t* h) {
            delete static_cast<TaskTimerData*>(h->data);
            delete (uv_timer_t*)h;
        });
    }

    void Scheduler::closePoll(uv_poll_t* poll) {
        auto* data = static_cast<TaskPollData*>(poll->data);
        if (!data->initialized) {
            delete data;
            delete poll;
            return;
        }

        uv_poll_stop(poll);
        uv_close((uv_handle_t*)poll, [](uv_handle_t* h) {
            delete static_cast<TaskPollData*>(h->data);
            delete (uv_poll_t*)h;
        });
    }
} // namespace Luwow::Task
