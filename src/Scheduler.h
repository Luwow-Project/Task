#pragma once

#include "lua.h"
#include "uv.h"

#include <unordered_map>
#include <cstdint>
#include <string>

namespace Luwow::Task {
    class Scheduler {
        public:
            Scheduler(lua_State* mainThread, uv_loop_t* loop);
            ~Scheduler();

            int spawn(lua_State* L);
            int defer(lua_State* L);
            int delay(lua_State* L, double seconds);
            int wait(lua_State* L, double seconds);
            int cancel(lua_State* L, lua_State* targetThread);

            uv_loop_t* getLoop() { return loop; };
        private:
            lua_State* mainThread;
            uv_loop_t* loop;

            // Maps a suspended lua_State (thread) to its active libuv timer
            std::unordered_map<lua_State*, uv_timer_t*> pendingTimers;

            static void delayTimerCb(uv_timer_t* handle);
            static void waitTimerCb(uv_timer_t* handle);
    };

    struct TaskTimerData {
        Scheduler* scheduler;
        lua_State* thread;
        int threadRef;
        int nargs;
        uint64_t startMs;
    };
} // namespace Luwow::Task