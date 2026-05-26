add_library(CFG SHARED CFGPlugin.cpp)
add_library(CFGFlattening SHARED CFGFlattening.cpp)

target_include_directories(CFGFlattening PUBLIC
    ${CMAKE_SOURCE_DIR}/libs/Dispatcher
)

target_link_libraries(CFGFlattening
    PRIVATE Dispatcher
)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_link_options(CFGFlattening PRIVATE
        "SHELL:-Wl,--whole-archive $<TARGET_FILE:Dispatcher> -Wl,--no-whole-archive"
    )
endif()

target_compile_features(CFG PUBLIC cxx_std_17)
