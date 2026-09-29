#include "Library.h"
#include "lualib.h"
#include "uv.h"

#include <stdexcept>

namespace Luwow::Task {
    static Library* getModuleInstance(lua_State* L) {
        Library* task = static_cast<Library*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!task) {
            throw std::runtime_error("Task module not found!");
        }
        return task;
    }

    Library* Library::instance = nullptr;

    Library::Library() : host(nullptr), scheduler(nullptr) {}

    Library::~Library() {
        if (scheduler) {
            delete scheduler;
        }
        Library::instance = nullptr;
    }

    void Library::pumpEvents() {
        if (!instance) return;
        Scheduler* scheduler = instance->scheduler;
        if (!scheduler) return;
        uv_run(scheduler->getLoop(), UV_RUN_DEFAULT);
    }

    int Library::schedulerCallback(lua_State* L, const std::string& chunkName, const std::string& bytecode, bool saveRef) {
        if (!instance) return 0;
        Scheduler* scheduler = instance->scheduler;
        if (!scheduler) return 0;
        
        lua_State* T = lua_newthread(L);
        lua_State* TL = lua_newthread(T);
        luaL_sandboxthread(TL);

        int result = luau_load(TL, chunkName.c_str(), bytecode.data(), bytecode.size(), 0);
        if (result != 0) {
            lua_xmove(TL, L, 1);
            lua_remove(L, -2);
            return 0;
        }

        instance->host->callDebuggerLuauCallback(TL, chunkName, true);
        scheduler->spawn(T);

        // If the thread has yielded, we run the uv loop here manually.
        while (lua_status(TL) == LUA_YIELD) {
            int hasActiveHandles = uv_run(scheduler->getLoop(), UV_RUN_ONCE);
            // If coroutine.yield() is called without a task.wait, there are no timers, so we error.
            if (hasActiveHandles == 0 && lua_status(TL) == LUA_YIELD) {
                lua_remove(L, -2);
                luaL_error(L, "Thread yielded with no active tasks.");
                return 0;
            }
        }

        int status = lua_status(TL);
        if (status != LUA_OK) {
            lua_remove(L, -2);
            return 0; 
        }

        if (saveRef) {
            if (lua_gettop(TL) != 1) {
                lua_remove(L, -1);
                luaL_error(L, "%s didn't return exactly one value", chunkName.c_str());
                return 0;
            }
            lua_xmove(TL, L, 1);
        } else {
            lua_settop(TL, 0); 
        }

        lua_remove(L, -2);
        return 1;
    }

    ILuauModule* Library::initialize(ILuauHost* host) {
        Library* task = new Library();
        task->setHost(host);

        uv_loop_t* loop = new uv_loop_t;
        uv_loop_init(loop);

        lua_State* mainThread = host->getMainState();
        task->scheduler = new Scheduler(mainThread, loop);
        Library::instance = task;

        host->setMessagePumpCallback(&Library::pumpEvents);
        host->setTaskSchedulerCallback(&Library::schedulerCallback);

        return task;
    }

    static int task_spawn(lua_State* L) {
        Library* library = getModuleInstance(L);
        Scheduler* scheduler = library->getScheduler();
        return scheduler->spawn(L);
    }

    static int task_delay(lua_State* L) {
        Library* library = getModuleInstance(L);
        Scheduler* scheduler = library->getScheduler();

        double seconds = luaL_checknumber(L, 1);
        if (seconds < 0) seconds = 0;
        lua_remove(L, 1);

        return scheduler->delay(L, seconds);
    }

    static int task_defer(lua_State* L) {
        Library* library = getModuleInstance(L);
        Scheduler* scheduler = library->getScheduler();
        return scheduler->defer(L);
    }

    static int task_wait(lua_State* L) {
        Library* library = getModuleInstance(L);
        Scheduler* scheduler = library->getScheduler();

        double seconds = luaL_optnumber(L, 1, 0);
        if (seconds < 0) seconds = 0;

        return scheduler->wait(L, seconds);
    }

    static int task_cancel(lua_State* L) {
        Library* library = getModuleInstance(L);
        Scheduler* scheduler = library->getScheduler();

        lua_State* target = lua_tothread(L, 1);
        if (!target) {
            luaL_error(L, "thread expected");
        }

        return scheduler->cancel(L, target);
    }

    const char* Library::getModuleName() const {
        return "task";
    }

    const char* Library::getModuleAlias() const {
        return "Luwow";
    }

    static LuauExport exports[] = {
        { "spawn", task_spawn },
        { "delay", task_delay },
        { "defer", task_defer },
        { "wait", task_wait },
        { "cancel", task_cancel },
        { nullptr, nullptr }
    };

    const LuauExport* Library::getExports() const {
        return exports;
    }
} // namespace Luwow::Task

LUWOW_REGISTER_MODULE(Luwow::Task::Library)