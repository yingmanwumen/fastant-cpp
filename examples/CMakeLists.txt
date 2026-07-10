# Build all examples
add_executable(example_basic basic.cpp)
target_compile_features(example_basic PRIVATE cxx_std_23)
target_link_libraries(example_basic PRIVATE fastant::fastant)

add_executable(example_anchor anchor.cpp)
target_compile_features(example_anchor PRIVATE cxx_std_23)
target_link_libraries(example_anchor PRIVATE fastant::fastant)

add_executable(example_longterm longterm.cpp)
target_compile_features(example_longterm PRIVATE cxx_std_23)
target_link_libraries(example_longterm PRIVATE fastant::fastant)
