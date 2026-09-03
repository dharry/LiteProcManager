// Copyright 2026 LiteProcManager Authors. All rights reserved.

#ifndef LITE_PROC_MANAGER_RESOURCE_H_
#define LITE_PROC_MANAGER_RESOURCE_H_

#define IDI_APP_ICON                    101
#define IDI_TB_TREE                     102
#define IDI_TB_LIST                     103
#define IDI_TB_COLUMNS                  104
#define IDI_TB_SHIELD                   105
#define IDI_TB_TOPMOST                  106
#define IDI_TB_TOPMOST_OFF              107
#define IDI_TB_OPTIONS                  108
#define IDI_TB_MONITOR                  109
#define IDI_TB_REFRESH                  110
#define IDI_TB_ENDTASK                  111

// Commands
#define IDM_REFRESH_NOW                 201
#define IDM_END_TASK                    202
#define IDM_END_TREE                    203
#define IDM_OPEN_FILE_LOCATION          204
#define IDM_SEARCH_ONLINE               205
#define IDM_PROPERTIES                  206
#define IDM_COPY_NAME                   207
#define IDM_COPY_PID                    208
#define IDM_COPY_PATH                   209
#define IDM_COPY_COMMAND_LINE           210
#define IDM_RESTART_AS_ADMIN            211
#define IDM_COPY_JSON                   212
#define IDM_COPY_TSV                    213
#define IDM_ABOUT                       214

#define IDM_PRIORITY_REALTIME           220
#define IDM_PRIORITY_HIGH               221
#define IDM_PRIORITY_ABOVE_NORMAL       222
#define IDM_PRIORITY_NORMAL             223
#define IDM_PRIORITY_BELOW_NORMAL       224
#define IDM_PRIORITY_IDLE               225

#define IDM_TOGGLE_TREE                 230
#define IDM_SELECT_COLUMNS              231
#define IDM_ALWAYS_ON_TOP               232
#define IDM_TOGGLE_THEME                233
#define IDM_OPTIONS                     234

#define IDM_TRAY_RESTORE                240
#define IDM_TRAY_EXIT                   241
#define IDM_MONITOR_SETTINGS            250
#define IDM_ADD_TO_MONITOR              251
#define IDM_SERVICE_START               260
#define IDM_SERVICE_STOP                261
#define IDM_SERVICE_RESTART             262
#define IDM_SERVICE_STARTUP_AUTO        263
#define IDM_SERVICE_STARTUP_MANUAL      264
#define IDM_SERVICE_STARTUP_DISABLED    265
#define IDM_SERVICE_GO_TO_PROCESS       266
#define IDM_PROCESS_GO_TO_SERVICE       267

// Controls
#define IDC_SEARCH_EDIT                 301
#define IDC_INTERVAL_COMBO              302
#define IDC_LISTVIEW                    303
#define IDC_TREEVIEW                    304
#define IDC_STATUSBAR                   305
#define IDC_BTN_TREE                    306
#define IDC_BTN_COLUMNS                 307
#define IDC_BTN_TOPMOST                 308
#define IDC_BTN_THEME                   309
#define IDC_BTN_REFRESH                 310
#define IDC_BTN_ENDTASK                 311
#define IDC_BTN_MONITOR                 312
#define IDC_BTN_OPTIONS                 313
#define IDC_BTN_RESTART_ADMIN           314
#define IDC_FILTER_COL_COMBO            315
#define IDC_FILTER_OP_COMBO             316
#define IDC_FILTER_VAL_EDIT             317
#define IDC_FILTER_CLEAR_BTN            318
#define IDC_MAIN_TAB                    330
#define IDC_SERVICE_LIST_VIEW           331
#define IDC_OPT_BTN_LIST_FONT           320
#define IDC_OPT_LBL_LIST_FONT_VAL       321
#define IDC_OPT_BTN_UI_FONT             322
#define IDC_OPT_LBL_UI_FONT_VAL         323
#define IDC_OPT_BTN_FONT                320
#define IDC_OPT_LBL_FONT_VAL            321

#ifndef IDC_STATIC
#define IDC_STATIC                      (-1)
#endif

// Dialog Resources
#define IDD_OPTIONS_DIALOG              2001
#define IDD_COLUMN_SELECTOR_DIALOG      2002
#define IDD_MONITOR_DIALOG              2003
#define IDD_RULE_EDIT_DIALOG            2004
#define IDD_ADD_EXCLUDED_DIALOG         2005

// Options Dialog Controls
#define IDC_OPT_COMBO_LANG              3001
#define IDC_OPT_COMBO_THEME             3002
#define IDC_OPT_EDIT_INTERVAL           3003
#define IDC_OPT_CHK_ALWAYS_TOP          3004
#define IDC_OPT_CHK_TRAY                3005
#define IDC_OPT_CHK_AUTO_START          3008
#define IDC_OPT_BTN_OK                  3006
#define IDC_OPT_BTN_CANCEL              3007
#define IDC_OPT_LBL_LANG                3012
#define IDC_OPT_LBL_THEME               3013
#define IDC_OPT_LBL_LIST_FONT           3014
#define IDC_OPT_LBL_UI_FONT             3015
#define IDC_OPT_LBL_INTERVAL            3016
#define IDC_OPT_LBL_SECONDS             3017
#define IDC_OPT_LBL_EXCLUDED            3018
#define IDC_OPT_LIST_EXCLUDED           3019
#define IDC_OPT_BTN_ADD_EXCLUDED        3020
#define IDC_OPT_BTN_DEL_EXCLUDED        3021
#define IDC_OPT_SLIDER_INTERVAL         3022
#define IDC_OPT_LBL_INTERVAL_VAL        3023

// Column Selector Dialog Controls
#define IDC_COL_INSTRUCTION             4001
#define IDC_COL_LIST                    4002
#define IDC_COL_BTN_MOVE_UP             4003
#define IDC_COL_BTN_MOVE_DOWN           4004
#define IDC_COL_BTN_SELECT_ALL          4005
#define IDC_COL_BTN_DEFAULT             4006

// Monitor Dialog Controls
#define IDC_MONITOR_LIST                5001
#define IDC_MONITOR_BTN_ADD             5002
#define IDC_MONITOR_BTN_EDIT            5003
#define IDC_MONITOR_BTN_DELETE          5004
#define IDC_MONITOR_BTN_TOGGLE          5005
#define IDC_MONITOR_BTN_TEST_WARN       5006
#define IDC_MONITOR_BTN_TEST_ERR        5007
#define IDC_MONITOR_BTN_REMOVE          5008

// Rule Edit Dialog Controls
#define IDC_RULE_EDIT_NAME              6001
#define IDC_RULE_COMBO_TARGET           6002
#define IDC_RULE_COMBO_MATCH            6003
#define IDC_RULE_EDIT_PATTERN           6004
#define IDC_RULE_COMBO_LEVEL            6005
#define IDC_RULE_COMBO_COL              6006
#define IDC_RULE_COMBO_OP               6007
#define IDC_RULE_EDIT_VALUE             6008
#define IDC_RULE_COMBO_LOGIC            6009
#define IDC_RULE_EDIT_COOLDOWN          6010
#define IDC_RULE_LBL_NAME               6011
#define IDC_RULE_LBL_TARGET             6012
#define IDC_RULE_LBL_PATTERN            6013
#define IDC_RULE_LBL_LEVEL              6014
#define IDC_RULE_LBL_CONDITION          6015
#define IDC_RULE_LBL_COOLDOWN           6016
#define IDC_RULE_LBL_SECONDS            6017
#define IDC_RULE_SLIDER_COOLDOWN        6018
#define IDC_RULE_LBL_COOLDOWN_VAL       6019
#define IDC_RULE_CHK_NOT_FOUND          6020

// Add Excluded Process Dialog Controls
#define IDC_ADD_EXCL_PROMPT             7001
#define IDC_ADD_EXCL_EDIT               7002

// Timers & Messages
#define IDT_REFRESH_TIMER               501
#define WM_APP_TRAYMSG                  (WM_APP + 1)

// ========================================================
// String Resources (STRINGTABLE)
// ========================================================
#define IDS_APP_TITLE                   10001
#define IDS_SEARCH_PLACEHOLDER          10002
#define IDS_LABEL_SEARCH                10003
#define IDS_LABEL_INTERVAL              10004
#define IDS_INTERVAL_PAUSE              10005
#define IDS_STATUS_PROCESSES            10006
#define IDS_STATUS_THREADS              10007
#define IDS_STATUS_HANDLES              10008
#define IDS_STATUS_CPU                  10009
#define IDS_STATUS_MEMORY               10010
#define IDS_STATUS_COMMIT               10011

// Menus
#define IDS_MENU_FILE                   10020
#define IDS_MENU_VIEW                   10021
#define IDS_MENU_OPTIONS                10022
#define IDS_MENU_MONITOR_RULES          10023
#define IDS_MENU_SELECT_COLUMNS         10024
#define IDS_MENU_ALWAYS_ON_TOP          10025
#define IDS_MENU_TOGGLE_THEME           10026
#define IDS_MENU_EXIT                   10027
#define IDS_MENU_OPEN                   10028
#define IDS_MENU_REFRESH_NOW            10029
#define IDS_MENU_END_PROCESS            10030
#define IDS_MENU_OPEN_LOCATION          10031
#define IDS_MENU_ONLINE_SEARCH          10032
#define IDS_MENU_PROPERTIES             10033
#define IDS_MENU_SET_PRIORITY           10034
#define IDS_MENU_COPY                   10035
#define IDS_MENU_COPY_CMD               10036
#define IDS_MENU_COPY_PATH              10037
#define IDS_MENU_END_TREE               10038
#define IDS_MENU_RESTART_AS_ADMIN       10039

// Tooltips
#define IDS_TOOLTIP_THEME_LIGHT         10040
#define IDS_TOOLTIP_THEME_DARK          10041
#define IDS_TOOLTIP_REFRESH             10042
#define IDS_TOOLTIP_OPTIONS             10043
#define IDS_TOOLTIP_MONITOR             10044
#define IDS_TOOLTIP_COLUMNS             10045
#define IDS_TOOLTIP_ALWAYS_ON_TOP       10046
#define IDS_TOOLTIP_VIEW_TREE           10047
#define IDS_TOOLTIP_VIEW_LIST           10048
#define IDS_TOOLTIP_TOPMOST_OFF         10049
#define IDS_TOOLTIP_TOPMOST_ON          10050
#define IDS_TOOLTIP_END_PROCESS         10051
#define IDS_TOOLTIP_QUICK_FILTER        10052
#define IDS_TOOLTIP_INTERVAL            10053
#define IDS_TOOLTIP_RESTART_ADMIN       10054
#define IDS_TOOLTIP_RUNNING_AS_ADMIN    10055

// Dialog Common
#define IDS_BTN_OK                      10060
#define IDS_BTN_CANCEL                  10061
#define IDS_BTN_SAVE                    10062
#define IDS_BTN_ADD                     10063
#define IDS_BTN_EDIT                    10064
#define IDS_BTN_DELETE                  10065
#define IDS_BTN_TOGGLE                  10066
#define IDS_BTN_MOVE_UP                 10067
#define IDS_BTN_MOVE_DOWN               10068
#define IDS_BTN_DEFAULT                 10069
#define IDS_BTN_TEST_WARN               10070
#define IDS_BTN_TEST_ERR                10071
#define IDS_CONFIRM_DELETE              10072
#define IDS_CONFIRM_DELETE_MULTIPLE     10073
#define IDS_CONFIRM_RESTART_THEME       10074
#define IDS_RESTART_NOTICE_TITLE        10075
#define IDS_CONFIRM_EXIT                10076
#define IDS_BTN_REMOVE                  10077
#define IDS_MENU_COPY_JSON              10270
#define IDS_MENU_COPY_TSV               10271
#define IDS_MENU_HELP                   10272
#define IDS_MENU_ABOUT                  10273
#define IDS_ABOUT_TITLE                 10274
#define IDS_ABOUT_APP_NAME              10275

// Options Dialog
#define IDS_DLG_OPTIONS_TITLE           10080
#define IDS_LABEL_LANGUAGE              10081
#define IDS_LABEL_THEME                 10082
#define IDS_LABEL_LIST_FONT             10083
#define IDS_LABEL_UI_FONT               10084
#define IDS_LABEL_FONT                  10085
#define IDS_BTN_CHANGE_FONT             10086
#define IDS_LABEL_REFRESH_INTERVAL      10087
#define IDS_LABEL_ALWAYS_ON_TOP         10088
#define IDS_LABEL_MINIMIZE_TO_TRAY      10089
#define IDS_LANG_AUTO                   10090
#define IDS_LANG_JAPANESE               10091
#define IDS_LANG_ENGLISH                10092
#define IDS_THEME_LIGHT                 10093
#define IDS_THEME_DARK                  10094
#define IDS_SECONDS_UNIT                10095
#define IDS_LABEL_AUTO_START            10096
#define IDS_LABEL_EXCLUDED_PROCESSES    10097
#define IDS_DLG_ADD_EXCLUDED_TITLE      10098
#define IDS_LABEL_ADD_EXCLUDED_PROMPT   10099

// Monitor Dialog
#define IDS_DLG_MONITOR_TITLE           10100
#define IDS_COL_STATUS                  10101
#define IDS_COL_RULE_NAME               10102
#define IDS_COL_LEVEL                   10103
#define IDS_COL_TARGET                  10104
#define IDS_COL_CONDITION               10105
#define IDS_STATUS_ENABLED              10106
#define IDS_STATUS_DISABLED             10107
#define IDS_LEVEL_WARNING               10108
#define IDS_LEVEL_CRITICAL              10109
#define IDS_MATCH_EXACT                 10110
#define IDS_MATCH_CONTAINS              10111
#define IDS_MATCH_STARTS_WITH           10112
#define IDS_MATCH_ENDS_WITH             10113
#define IDS_MATCH_TARGET_PROCESS_NAME   10114
#define IDS_MATCH_TARGET_PID            10115
#define IDS_RULE_EDIT_TITLE_ADD         10116
#define IDS_RULE_EDIT_TITLE_EDIT        10117
#define IDS_RULE_NAME_LABEL             10118
#define IDS_TARGET_TYPE_LABEL           10119
#define IDS_MATCH_TYPE_LABEL            10120
#define IDS_PATTERN_LABEL               10121
#define IDS_CONDITION_LABEL             10122
#define IDS_COOLDOWN_LABEL              10123
#define IDS_UNIT_SECONDS_COOLDOWN       10124
#define IDS_LOGIC_AND                   10125
#define IDS_LOGIC_OR                    10126
#define IDS_LABEL_RULE_NAME             10127
#define IDS_LABEL_TARGET                10128
#define IDS_LABEL_LEVEL                 10129
#define IDS_LABEL_LOGIC                 10130
#define IDS_OP_CONTAINS                 10131
#define IDS_ITEM_DEFAULT                10132
#define IDS_MSG_RULE_NAME_EMPTY         10133
#define IDS_MSG_PATTERN_EMPTY           10134
#define IDS_MSG_SELECT_RULE_TO_EDIT     10135
#define IDS_MSG_SELECT_RULE_TO_DELETE   10136
#define IDS_MSG_SELECT_RULE_TO_TOGGLE   10137
#define IDS_LABEL_NOTIFY_IF_NOT_FOUND   10138
#define IDS_MSG_PROCESS_NOT_FOUND       10139

// Column Selector Dialog
#define IDS_DLG_COL_SELECTOR_TITLE      10140
#define IDS_SELECT_COLUMNS_INSTRUCTION  10141
#define IDS_BTN_SELECT_ALL              10142
#define IDS_BTN_DESELECT_ALL            10143

// Priority Names
#define IDS_PRIORITY_REALTIME           10150
#define IDS_PRIORITY_HIGH               10151
#define IDS_PRIORITY_ABOVE_NORMAL       10152
#define IDS_PRIORITY_NORMAL             10153
#define IDS_PRIORITY_BELOW_NORMAL       10154
#define IDS_PRIORITY_LOW                10155

// Process Details & Snapshot Status
#define IDS_STATUS_RUNNING              10160
#define IDS_STATUS_SUSPENDED            10161
#define IDS_YES                         10162
#define IDS_NO                          10163
#define IDS_ENABLED                     10164
#define IDS_DISABLED                    10165
#define IDS_ENABLED_PERMANENT           10166
#define IDS_NOT_APPLICABLE              10167
#define IDS_PLATFORM_32BIT              10168
#define IDS_PLATFORM_64BIT              10169
#define IDS_PROPERTIES_TITLE_PREFIX     10170
#define IDS_PROPERTIES_HEADER           10171
#define IDS_PROP_IMAGE_NAME             10172
#define IDS_PROP_PID                    10173
#define IDS_PROP_PARENT_PID             10174
#define IDS_PROP_STATUS                 10175
#define IDS_PROP_USER                   10176
#define IDS_PROP_DESCRIPTION            10177
#define IDS_PROP_ARCHITECTURE           10178
#define IDS_PROP_PRIORITY               10179
#define IDS_PROP_THREADS                10180
#define IDS_PROP_HANDLES                10181
#define IDS_PROP_CPU                    10182
#define IDS_PROP_WORKING_SET            10183
#define IDS_PROP_COMMIT                 10184
#define IDS_PROP_START_TIME             10185
#define IDS_PROP_FILE_PATH              10186

// Message Boxes & Prompts
#define IDS_MSG_CONFIRM_END_PROCESS     10190
#define IDS_MSG_CONFIRM_END_PROCESS_MULTIPLE 10209
#define IDS_TITLE_CONFIRM_END_PROCESS   10191
#define IDS_MSG_END_PROCESS_ERROR       10192
#define IDS_TITLE_ERROR                 10193
#define IDS_MSG_CONFIRM_END_TREE        10194
#define IDS_TITLE_CONFIRM_END_TREE      10195
#define IDS_MSG_END_TREE_PARTIAL        10196
#define IDS_TITLE_NOTICE                10197
#define IDS_TITLE_INFO                  10198
#define IDS_MSG_WARN_REALTIME           10199
#define IDS_TITLE_CHANGE_PRIORITY       10200
#define IDS_MSG_CHANGE_PRIORITY_ERROR   10201
#define IDS_MSG_ACCESS_DENIED           10202
#define IDS_MSG_FILE_NOT_FOUND          10203
#define IDS_MSG_REALTIME_WARNING        10204
#define IDS_MSG_TRAY_MINIMIZED          10205
#define IDS_MSG_ERROR                   10206
#define IDS_MSG_INFO                    10207
#define IDS_MSG_WARNING                 10208

// Process Columns Header Names (41 Columns)
#define IDS_COL_HDR_NAME                10220
#define IDS_COL_HDR_PID                 10221
#define IDS_COL_HDR_STATUS              10222
#define IDS_COL_HDR_USER_NAME           10223
#define IDS_COL_HDR_CPU                 10224
#define IDS_COL_HDR_WORKING_SET         10225
#define IDS_COL_HDR_PRIVATE_WS          10226
#define IDS_COL_HDR_PEAK_WS             10227
#define IDS_COL_HDR_WS_DELTA            10228
#define IDS_COL_HDR_COMMIT_SIZE         10229
#define IDS_COL_HDR_PAGED_POOL          10230
#define IDS_COL_HDR_NON_PAGED_POOL      10231
#define IDS_COL_HDR_THREADS             10232
#define IDS_COL_HDR_HANDLES             10233
#define IDS_COL_HDR_BASE_PRIORITY       10234
#define IDS_COL_HDR_DESCRIPTION         10235
#define IDS_COL_HDR_OS_CONTEXT          10236
#define IDS_COL_HDR_FILE_PATH           10237
#define IDS_COL_HDR_COMMAND_LINE        10238
#define IDS_COL_HDR_USER_OBJECTS        10239
#define IDS_COL_HDR_GDI_OBJECTS         10240
#define IDS_COL_HDR_IO_READ_COUNT       10241
#define IDS_COL_HDR_IO_READ_BYTES       10242
#define IDS_COL_HDR_IO_WRITE_COUNT      10243
#define IDS_COL_HDR_IO_WRITE_BYTES      10244
#define IDS_COL_HDR_IO_OTHER_COUNT      10245
#define IDS_COL_HDR_IO_OTHER_BYTES      10246
#define IDS_COL_HDR_ELEVATED            10247
#define IDS_COL_HDR_UAC_VIRTUALIZATION  10248
#define IDS_COL_HDR_DEP_STATUS          10249
#define IDS_COL_HDR_ENTERPRISE_CONTEXT  10250
#define IDS_COL_HDR_DPI_AWARENESS       10251
#define IDS_COL_HDR_PACKAGE_NAME        10252
#define IDS_COL_HDR_ARCHITECTURE        10253
#define IDS_COL_HDR_PLATFORM            10254
#define IDS_COL_HDR_GPU_USAGE           10255
#define IDS_COL_HDR_GPU_ENGINE          10256
#define IDS_COL_HDR_DEDICATED_GPU_MEM   10257
#define IDS_COL_HDR_SHARED_GPU_MEM      10258
#define IDS_COL_HDR_SESSION_ID          10259
#define IDS_COL_HDR_CREATE_TIME         10260

// Services Management
#define IDS_TAB_PROCESSES               10301
#define IDS_TAB_SERVICES                10302
#define IDS_SVC_COL_NAME                10303
#define IDS_SVC_COL_DISPLAY_NAME        10304
#define IDS_SVC_COL_PID                 10305
#define IDS_SVC_COL_STATE               10306
#define IDS_SVC_COL_START_TYPE          10307
#define IDS_SVC_COL_ACCOUNT             10308
#define IDS_SVC_COL_DESCRIPTION         10309
#define IDS_SVC_STATE_RUNNING           10310
#define IDS_SVC_STATE_STOPPED           10311
#define IDS_SVC_STATE_START_PENDING     10312
#define IDS_SVC_STATE_STOP_PENDING      10313
#define IDS_SVC_STATE_PAUSED            10314
#define IDS_SVC_STATE_PAUSE_PENDING     10315
#define IDS_SVC_STATE_CONTINUE_PENDING  10316
#define IDS_SVC_STATE_UNKNOWN           10317
#define IDS_SVC_START_TYPE_AUTO         10318
#define IDS_SVC_START_TYPE_MANUAL       10319
#define IDS_SVC_START_TYPE_DISABLED     10320
#define IDS_SVC_START_TYPE_BOOT         10321
#define IDS_SVC_START_TYPE_SYSTEM       10322
#define IDS_SVC_START_TYPE_UNKNOWN      10323
#define IDS_MENU_SERVICE_START          10324
#define IDS_MENU_SERVICE_STOP           10325
#define IDS_MENU_SERVICE_RESTART        10326
#define IDS_MENU_SERVICE_STARTUP        10327
#define IDS_MENU_SERVICE_GO_TO_PROCESS  10328
#define IDS_MENU_PROCESS_GO_TO_SERVICE  10329
#define IDS_STATUS_SERVICE_COUNTS       10330

#ifdef APSTUDIO_INVOKED
#ifndef APSTUDIO_READONLY_SYMBOLS
#define _APS_NO_MFC                     1
#define _APS_NEXT_RESOURCE_VALUE        2005
#define _APS_NEXT_COMMAND_VALUE         32771
#define _APS_NEXT_CONTROL_VALUE         7000
#define _APS_NEXT_SYMED_VALUE           102
#endif
#endif

#endif  // LITE_PROC_MANAGER_RESOURCE_H_
