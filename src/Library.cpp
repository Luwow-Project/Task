#include "Library.h"
#include "lualib.h"
#include "uv.h"

#include <cstdio>
#include <stdexcept>

namespace Luwow::Task {
    static Library* getModuleInstance(lua_State* L) {
        Library* task = static_cast<Library*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (!task) {
            throw std::runtime_error("Task module not found!");
        }
        return task;
    }

    Library::Library() : host(nullptr), scheduler(nullptr) {}

    Library::~Library() {
        if (scheduler) {
            delete scheduler;
        }
    }

    void Library::run() {
        if (!scheduler) return;
        scheduler->run();
    }

    void Library::stop() {
        if (!scheduler) return;
        scheduler->stop();
    }

    Luwow::Engine::RunMode Library::getRunMode() const {
        return LUWOW_MODULE_RUN_MODE;
    }

    // Spawns the requested threads from the bus.
    void Library::spawnRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        message.result = task->scheduler->runThread(message.state);
    }

    // Watches a socket for another library, resuming message.state once the socket is readable
    void Library::watchRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        uv_os_sock_t socket = static_cast<uv_os_sock_t>(std::stoull(message.data));
        task->scheduler->watch(message.state, socket);
        message.result = 1;
    }

    void Library::unwatchRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        message.result = task->scheduler->unwatch(message.state) ? 1 : 0;
    }

    // Resumes message.state with its message.result top values, now, next cycle or after message.data seconds
    void Library::resumeRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        message.result = task->scheduler->resumeNow(message.state, message.result);
    }

    void Library::deferRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        task->scheduler->resumeLater(message.state, message.result, 0);
    }

    void Library::delayRequest(void* context, Luwow::Engine::Message& message) {
        Library* task = static_cast<Library*>(context);
        double seconds = message.data.empty() ? 0 : std::stod(message.data);
        task->scheduler->resumeLater(message.state, message.result, seconds);
    }

    ILuauModule* Library::initialize(ILuauHost* host) {
        Library* task = new Library();
        task->host = host;

        bool parallel = task->getRunMode() == Luwow::Engine::RunMode::Parallel;
        task->scheduler = new Scheduler(host, host->getMainState(), parallel);

        if (!host->handle(Luwow::Engine::Topics::SchedulerSpawn, &Library::spawnRequest, task)) {
            fprintf(stderr, "[Task Scheduler] %s is already handled, scripts won't run through task\n", Luwow::Engine::Topics::SchedulerSpawn);
        }
        host->handle(Luwow::Engine::Topics::SchedulerResume, &Library::resumeRequest, task);
        host->handle(Luwow::Engine::Topics::SchedulerDefer, &Library::deferRequest, task);
        host->handle(Luwow::Engine::Topics::SchedulerDelay, &Library::delayRequest, task);
        host->handle(Luwow::Engine::Topics::SchedulerWatch, &Library::watchRequest, task);
        host->handle(Luwow::Engine::Topics::SchedulerUnwatch, &Library::unwatchRequest, task);

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