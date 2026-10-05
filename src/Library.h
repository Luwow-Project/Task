#pragma once

#include "Scheduler.h"
#include "ILuauModule.h"
#include "ILuauHost.h"
#include "lua.h"

namespace Luwow::Task {
    using ILuauModule = Luwow::Engine::ILuauModule;
    using ILuauHost = Luwow::Engine::ILuauHost;
    using RunMode = Luwow::Engine::RunMode;

    class Library : public ILuauModule {
    public:
        Library();
        ~Library();

        ILuauModule* initialize(ILuauHost* host) override;

        const char* getModuleName() const override;
        const char* getModuleAlias() const override;
        const LuauExport* getExports() const override;
        
        RunMode getRunMode() const override;
        Scheduler* getScheduler() const { return scheduler; }

        // Handles scheduler-spawn requests
        static void spawnRequest(void* context, Luwow::Engine::Message& message);
        // Handle scheduler-watch and scheduler-unwatch requests
        static void watchRequest(void* context, Luwow::Engine::Message& message);
        static void unwatchRequest(void* context, Luwow::Engine::Message& message);
        // Handle scheduler-resume, scheduler-defer and scheduler-delay requests
        static void resumeRequest(void* context, Luwow::Engine::Message& message);
        static void deferRequest(void* context, Luwow::Engine::Message& message);
        static void delayRequest(void* context, Luwow::Engine::Message& message);

        // Serial: runs the remaining tasks after the main script. Parallel: runs the loop on the module's thread.
        void run() override;
        void stop() override;
    private:
        ILuauHost* host = nullptr;
        Scheduler* scheduler = nullptr;
    };
} // namespace Luwow::Task
