#pragma once

#include "Scheduler.h"
#include "ILuauModule.h"
#include "ILuauHost.h"
#include "lua.h"

namespace Luwow::Task {
    using ILuauModule = Luwow::Engine::ILuauModule;
    using ILuauHost = Luwow::Engine::ILuauHost;

    class Library : public ILuauModule {
    public:
        Library();
        ~Library();

        ILuauModule* initialize(ILuauHost* host) override;

        const char* getModuleName() const override;
        const char* getModuleAlias() const override;
        const LuauExport* getExports() const override;

        void setHost(ILuauHost* host) { this->host = host; };
        static int schedulerCallback(lua_State* L, const std::string& chunkName, const std::string& bytecode, bool saveRef);
        static void pumpEvents();

        Scheduler* getScheduler() const { return scheduler; }
    private:
        ILuauHost* host = nullptr;
        Scheduler* scheduler = nullptr;
        static Library* instance; // For the event loop.
    };
} // namespace Luwow::Task