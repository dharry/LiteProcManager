set(TEST_SOURCES
    lite_proc_manager_test.cc
    ../src/language_manager.cc
    ../src/process_snapshot_service.cc
    ../src/process_item.cc
    ../src/process_column.cc
    ../src/monitor_rule.cc
    ../src/monitor_service.cc
    ../src/toast_window.cc
    ../src/app_settings.cc
    ../src/resource.rc
)

add_library(LiteProcManagerTests SHARED ${TEST_SOURCES})

target_include_directories(LiteProcManagerTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(LiteProcManagerTests PRIVATE
    version
    advapi32
    user32
    gdi32
    shell32
    dwmapi
)
