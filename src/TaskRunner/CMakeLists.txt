add_executable(Luwow.RunScriptWithTask)
target_include_directories(Luwow.RunScriptWithTask PRIVATE
    ${TASK_ROOT}/src/TaskLibrary
    ${TASK_ROOT}/src/TaskRunner
    ${LIBUV_ROOT}/include
    ${ENGINE_ROOT}
)

target_link_libraries(Luwow.RunScriptWithTask PRIVATE
    Luwow.Engine
    Luwow.TaskLibrary
    Luau.Ast
    Luau.Compiler
    Luau.VM
    Luau.Common
    uv_a
)

set_target_properties(Luwow.RunScriptWithTask PROPERTIES OUTPUT_NAME runscriptwithtasklib)
target_sources(Luwow.RunScriptWithTask PRIVATE main.cpp)
