# runtime/CMakeLists.txt — Runtime shipped with compiled binaries.
add_library(aegis_runtime STATIC
    core/src/panic.cpp
    alloc/src/bump.cpp
    alloc/src/pool.cpp
    alloc/src/system.cpp
    sync/src/atomic.cpp
    sync/src/mutex.cpp
    sync/src/rwlock.cpp
    sync/src/channel.cpp
    io/src/file.cpp
    io/src/net.cpp
    io/src/syscall.cpp
)
target_include_directories(aegis_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/alloc/include
    ${CMAKE_CURRENT_SOURCE_DIR}/sync/include
    ${CMAKE_CURRENT_SOURCE_DIR}/io/include
)
target_link_libraries(aegis_runtime PUBLIC aegis_compile_options)
