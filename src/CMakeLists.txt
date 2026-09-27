add_library(Luwow.TaskLibrary STATIC)
target_include_directories(Luwow.TaskLibrary PRIVATE
    ${TASK_ROOT}/src
    ${LIBUV_ROOT}/include
    ${ENGINE_ROOT}
)
target_link_libraries(Luwow.TaskLibrary PRIVATE 
    Luwow.Engine
    uv_a
)

target_sources(Luwow.TaskLibrary PRIVATE
    Library.h
    Library.cpp
    Scheduler.h
    Scheduler.cpp
)
