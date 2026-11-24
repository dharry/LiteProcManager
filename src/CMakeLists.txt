set(SOURCES
    main.cc
    main_window.cc
    column_selector_dialog.cc
    icon_helper.cc
    theme_manager.cc
    process_snapshot_service.cc
    process_item.cc
    process_column.cc
    process_export.cc
    service_item.cc
    service_enumeration_helper.cc
    service_manager_service.cc
    monitor_rule.cc
    monitor_service.cc
    monitor_dialog.cc
    performance_data_service.cc
    performance_formatter.cc
    performance_graph_renderer.cc
    performance_view.cc
    cpu_performance_collector.cc
    memory_network_collector.cc
    toast_window.cc
    app_settings.cc
    options_dialog.cc
    language_manager.cc
    ../third_party/jsoncpp/src/jsoncpp.cpp
    resource.rc
)

add_executable(LiteProcManager WIN32 ${SOURCES})

target_include_directories(LiteProcManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/jsoncpp/include
)

target_link_libraries(LiteProcManager PRIVATE
    comctl32
    ole32
    dwmapi
    uxtheme
    shell32
    comdlg32
    version
    advapi32
    user32
    gdi32
    iphlpapi
    powrprof
    ws2_32
    msimg32
)
