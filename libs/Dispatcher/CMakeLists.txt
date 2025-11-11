add_library(Dispatcher STATIC
    MinimalTDispatcher.cpp
    TrivialDispatcher.cpp
)

set_target_properties(Dispatcher PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(Dispatcher PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(Dispatcher PUBLIC cxx_std_17)