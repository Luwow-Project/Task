luwow_add_library(Luwow.TaskLibrary
    NAME task
    SOURCES
        Library.h
        Library.cpp
        Scheduler.h
        Scheduler.cpp
)
target_include_directories(Luwow.TaskLibrary PRIVATE
    ${TASK_ROOT}/src
    ${LIBUV_ROOT}/include
)
target_link_libraries(Luwow.TaskLibrary PRIVATE uv_a)
