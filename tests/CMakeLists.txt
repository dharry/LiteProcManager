set(TEST_SOURCES
    lite_proc_manager_test.cc
    performance_formatter_test.cc
    cpu_performance_collector_test.cc
    memory_network_collector_test.cc
    performance_graph_renderer_test.cc
    performance_data_service_test.cc
    ../src/language_manager.cc
    ../src/icon_helper.cc
    ../src/process_snapshot_service.cc
    ../src/process_item.cc
    ../src/process_column.cc
    ../src/process_export.cc
    ../src/service_item.cc
    ../src/service_enumeration_helper.cc
    ../src/service_manager_service.cc
    ../src/monitor_rule.cc
    ../src/monitor_service.cc
    ../src/performance_data_service.cc
    ../src/performance_formatter.cc
    ../src/performance_graph_renderer.cc
    ../src/cpu_performance_collector.cc
    ../src/memory_network_collector.cc
    ../src/toast_window.cc
    ../src/app_settings.cc
    ../third_party/jsoncpp/src/jsoncpp.cpp
    ../src/resource.rc
)

if(MSVC)
    get_filename_component(_vs_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_vs_vc_dir "${_vs_cl_dir}/../../../../.." ABSOLUTE)
    find_path(VS_UNITTEST_INCLUDE_DIR "CppUnitTest.h"
        HINTS
            "$ENV{VCINSTALLDIR}/Auxiliary/VS/UnitTest/include"
            "${_vs_vc_dir}/Auxiliary/VS/UnitTest/include"
    )
    find_library(VS_UNITTEST_LIBRARY
        NAMES Microsoft.VisualStudio.TestTools.CppUnitTestFramework
        HINTS
            "$ENV{VCINSTALLDIR}/Auxiliary/VS/UnitTest/lib/x64"
            "${_vs_vc_dir}/Auxiliary/VS/UnitTest/lib/x64"
    )
    find_program(VSTEST_CONSOLE_EXE
        NAMES vstest.console.exe
        HINTS
            "$ENV{VSINSTALLDIR}/Common7/IDE/CommonExtensions/Microsoft/TestWindow"
            "${_vs_vc_dir}/../Common7/IDE/CommonExtensions/Microsoft/TestWindow"
    )
endif()

if(NOT VS_UNITTEST_INCLUDE_DIR OR NOT VS_UNITTEST_LIBRARY)
    message(WARNING "Visual Studio CppUnitTestFramework not found; skipping LiteProcManagerTests target.")
    return()
endif()

add_library(LiteProcManagerTests SHARED ${TEST_SOURCES})

target_include_directories(LiteProcManagerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/jsoncpp/include
    ${VS_UNITTEST_INCLUDE_DIR}
)

target_link_libraries(LiteProcManagerTests PRIVATE
    ${VS_UNITTEST_LIBRARY}
    comctl32
    version
    advapi32
    user32
    gdi32
    shell32
    dwmapi
    iphlpapi
    powrprof
    ws2_32
    msimg32
)

if(VSTEST_CONSOLE_EXE)
    add_test(
        NAME LiteProcManagerTests
        COMMAND "${VSTEST_CONSOLE_EXE}" "$<TARGET_FILE:LiteProcManagerTests>" "--logger:Console;verbosity=minimal"
    )
else()
    message(WARNING "vstest.console.exe not found; CTest test entry will not be registered.")
endif()
