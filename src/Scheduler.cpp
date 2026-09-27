#include "Scheduler.h"
#include "lualib.h"
#include "uv.h"

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

    Scheduler::Scheduler(lua_State* mainThread, uv_loop_t* loop)
        : mainThread(mainThread), loop(loop) {}

    Scheduler::~Scheduler() {
        for (auto& pair : pendingTimers) {
            uv_timer_t* timer = pair.second;
            auto* data = (TaskTimerData*)timer->data;

            lua_unref(mainThread, data->threadRef);
            delete data;

            uv_close((uv_handle_t*)timer, [](uv_handle_t* h) { 
                delete (uv_timer_t*)h; 
            });
        }
        pendingTimers.clear();
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

        auto* timer = new uv_timer_t;
        uv_timer_init(loop, timer);

        auto* data = new TaskTimerData { this, TL, ref, nargs, 0 };
        timer->data = data;

        uint64_t ms = (uint64_t)(seconds * 1000.0);
        uv_timer_start(timer, delayTimerCb, ms, 0);

        pendingTimers[TL] = timer;

        lua_settop(L, 1);
        return 1;
    }

    int Scheduler::wait(lua_State* L, double seconds) {
        lua_pushthread(L);
        lua_State* TL = lua_tothread(L, -1);
        int ref = lua_ref(L, -1);
        lua_pop(L, 1);

        auto* timer = new uv_timer_t;
        uv_timer_init(loop, timer);

        auto* data = new TaskTimerData { this, TL, ref, 0, uv_now(loop) };
        timer->data = data;

        uint64_t ms = (uint64_t)(seconds * 1000.0);
        uv_timer_start(timer, waitTimerCb, ms, 0);

        pendingTimers[TL] = timer;

        return lua_yield(L, 0);
    }

    int Scheduler::cancel(lua_State* L, lua_State* targetThread) {
        auto it = pendingTimers.find(targetThread);
        if (it != pendingTimers.end()) {
            uv_timer_t* timer = it->second;
            auto* data = (TaskTimerData*)timer->data;

            lua_unref(mainThread, data->threadRef);
            delete data;

            uv_close((uv_handle_t*)timer, [](uv_handle_t* h) { 
                delete (uv_timer_t*)h; 
            });

            pendingTimers.erase(it);
        }
        return 0;
    }

    // Callback class methods

    void Scheduler::delayTimerCb(uv_timer_t* handle) {
        auto* data = (TaskTimerData*)handle->data;
        Scheduler* self = data->scheduler;
        self->pendingTimers.erase(data->thread);

        lua_State* TL = data->thread;
        
        int res = lua_resume(TL, self->mainThread, data->nargs);
        if (res != LUA_OK && res != LUA_YIELD && res != LUA_BREAK) {
            if (lua_isstring(TL, -1)) {
                fprintf(stderr, "[Task Scheduler] Error in delay: %s\n", lua_tostring(TL, -1));
            }
            lua_pop(TL, 1);
        }

        lua_unref(self->mainThread, data->threadRef);
        delete data;
        uv_close((uv_handle_t*)handle, [](uv_handle_t* h) { delete (uv_timer_t*)h; });
    }

    void Scheduler::waitTimerCb(uv_timer_t* handle) {
        auto* data = (TaskTimerData*)handle->data;
        Scheduler* self = data->scheduler;
        self->pendingTimers.erase(data->thread);

        double elapsed = (uv_now(self->loop) - data->startMs) / 1000.0;

        lua_State* TL = data->thread;
        lua_pushnumber(TL, elapsed);

        int res = lua_resume(TL, self->mainThread, 1);
        if (res != LUA_OK && res != LUA_YIELD && res != LUA_BREAK) {
            if (lua_isstring(TL, -1)) {
                fprintf(stderr, "[Task Scheduler] Error in wait: %s\n", lua_tostring(TL, -1));
            }
            lua_pop(TL, 1);
        }

        lua_unref(self->mainThread, data->threadRef);
        delete data;
        uv_close((uv_handle_t*)handle, [](uv_handle_t* h) { delete (uv_timer_t*)h; });
    }
} // namespace Luwow::Task