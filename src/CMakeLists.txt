set(SOURCES
    main.cc
    main_window.cc
    column_selector_dialog.cc
    icon_helper.cc
    theme_manager.cc
    process_snapshot_service.cc
    process_item.cc
    process_column.cc
    monitor_rule.cc
    monitor_service.cc
    monitor_dialog.cc
    toast_window.cc
    app_settings.cc
    options_dialog.cc
    language_manager.cc
    resource.rc
)

add_executable(LiteProcManager WIN32 ${SOURCES})

target_include_directories(LiteProcManager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_link_libraries(LiteProcManager PRIVATE
    comctl32
    dwmapi
    uxtheme
    shell32
    version
    advapi32
    user32
    gdi32
)
