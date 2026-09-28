# Unit tests of MyBench.Framework against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_framework tst_framework.cpp)
target_link_libraries(tst_framework PRIVATE mybench_framework Qt6::Test)
if(NOT MSVC)
    target_compile_options(tst_framework PRIVATE -Wall -Wextra)
endif()
add_test(NAME framework COMMAND tst_framework)
set_tests_properties(framework PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
