#pragma once

#include "Scheduler.h"
#include "Engine.h"
#include "lua.h"

namespace Luwow::Task {
    using ILuauModule = Luwow::Engine::ILuauModule;
    using Engine = Luwow::Engine::Engine;

    class Library : public ILuauModule {
    public:
        Library();
        ~Library();

        ILuauModule* initialize(Engine* engine) override;

        const char* getModuleName() const override;
        const char* getModuleAlias() const override;
        const LuauExport* getExports() const override;

        void setEngine(Engine* engine) { this->engine = engine; };
        static int schedulerCallback(lua_State* L, const std::string& chunkName, const std::string& bytecode, bool saveRef);
        static void pumpEvents();

        Scheduler* getScheduler() const { return scheduler; }
    private:
        Engine* engine = nullptr;
        Scheduler* scheduler = nullptr;
        static Library* instance; // For the event loop.
    };
} // namespace Luwow::Task