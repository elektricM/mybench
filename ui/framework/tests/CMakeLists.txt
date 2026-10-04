# Unit tests of MyBench.Framework against the simulated myDAQ (no hardware, no window).
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_framework tst_framework.cpp)
target_link_libraries(tst_framework PRIVATE mybench_framework Qt6::Test)
if(NOT MSVC)
    target_compile_options(tst_framework PRIVATE -Wall -Wextra)
endif()
add_test(NAME framework COMMAND tst_framework)
set_tests_properties(framework PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")

# Launcher (Home favorites, user tools, pinned files, recent exports) and AppInfo About fields.
qt_add_executable(tst_launcher tst_launcher.cpp)
# (WorkspaceState is tested by app/tests/tst_workspace.qml.)
target_link_libraries(tst_launcher PRIVATE mybench_framework mybench_frameworkplugin Qt6::Test Qt6::Quick)
if(NOT MSVC)
    target_compile_options(tst_launcher PRIVATE -Wall -Wextra)
endif()
add_test(NAME launcher COMMAND tst_launcher)
set_tests_properties(launcher PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")

# Framework hooks for instruments (panel close, stale results, validation, capabilities, ...).
qt_add_executable(tst_hooks tst_hooks.cpp)
target_link_libraries(tst_hooks PRIVATE mybench_framework Qt6::Test)
if(NOT MSVC)
    target_compile_options(tst_hooks PRIVATE -Wall -Wextra)
endif()
add_test(NAME framework_hooks COMMAND tst_hooks)
set_tests_properties(framework_hooks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")

# Application-shell rows of docs/audit.md: toast stack and pause toasts, event log warnings and
# details, hot-plug polling rate, simulator controls.
qt_add_executable(tst_shellrows tst_shellrows.cpp)
target_link_libraries(tst_shellrows PRIVATE mybench_framework Qt6::Test)
if(NOT MSVC)
    target_compile_options(tst_shellrows PRIVATE -Wall -Wextra)
endif()
add_test(NAME framework_shellrows COMMAND tst_shellrows)
set_tests_properties(framework_shellrows PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;MYBENCH_NO_USB=1")
