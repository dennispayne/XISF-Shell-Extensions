// HostResources.h - Resource identifiers for the settings dialog.
#pragma once

#define IDD_SETTINGS                 101
#define IDD_ADVANCED                 102
#define IDI_SETTINGS_APP             201

#define IDC_STATIC_INTRO             1000
#define IDC_BTN_TOGGLE_PROPERTY      1001
#define IDC_BTN_TOGGLE_PREVIEW       1002
#define IDC_STATIC_PROPERTY_STATUS   1003
#define IDC_STATIC_PREVIEW_STATUS    1004
#define IDC_STATIC_PROPERTY_VER      1005
#define IDC_STATIC_PREVIEW_VER       1006

#define IDC_STATIC_CATALOGS          1010
#define IDC_STATIC_PIN               1011

#define IDC_STATIC_NGC_LABEL         1020
#define IDC_STATIC_NGC_GITHUB        1021
#define IDC_STATIC_NGC_LOCAL         1022
#define IDC_STATIC_NGC_MATCH         1023
#define IDC_STATIC_ADD_LABEL         1024
#define IDC_STATIC_ADD_GITHUB        1025
#define IDC_STATIC_ADD_LOCAL         1026
#define IDC_STATIC_ADD_MATCH         1027

#define IDC_BTN_FETCH_NGC            1030
#define IDC_BTN_FETCH_ADD            1031
#define IDC_BTN_REMOVE_NGC           1032
#define IDC_BTN_REMOVE_ADD           1033
#define IDC_BTN_IMPORT_FILE          1034
#define IDC_BTN_OPEN_CATALOG_DIR     1035
#define IDC_BTN_COPY_EXPECTED_HASHES 1036
#define IDC_PROGRESS                 1037
#define IDC_STATIC_PROGRESS_TEXT     1038
#define IDC_LINK_OPENNGC_COMMIT      1039
#define IDC_BTN_RESTART_EXPLORER     1040
#define IDC_BTN_FLUSH_THUMBCACHE     1041
#define IDC_BTN_REGISTER_HANDLERS    1042
#define IDC_STATIC_VERSION           1043
#define IDC_BTN_ADVANCED             1044
#define IDC_LINK_HASH_HELP           1045

#define IDC_BTN_TRACE_START          1060
#define IDC_BTN_TRACE_STOP           1061
#define IDC_BTN_TRACE_VIEW           1062
#define IDC_STATIC_TRACE_PATH        1063
#define IDC_STATIC_TRACE_STATUS      1064
#define IDC_CHK_TRACE_PROPERTY       1065
#define IDC_CHK_TRACE_PREVIEW        1066
#define IDC_CHK_TRACE_LEVEL_INFO     1067
#define IDC_CHK_TRACE_LEVEL_VERBOSE  1068
#define IDC_BTN_TRACE_OPEN_ETL       1069
#define IDC_BTN_TRACE_EXPORT_XML     1070

#define IDC_CHK_TRACE_PROP_ERROR     1071
#define IDC_CHK_TRACE_PROP_WARN      1072
#define IDC_CHK_TRACE_PROP_INFO      1073
#define IDC_CHK_TRACE_PROP_VERBOSE   1074
#define IDC_CHK_TRACE_PREV_ERROR     1075
#define IDC_CHK_TRACE_PREV_WARN      1076
#define IDC_CHK_TRACE_PREV_INFO      1077
#define IDC_CHK_TRACE_PREV_VERBOSE   1078

#define IDC_CHK_TRACE_FILT_ERROR     1090
#define IDC_CHK_TRACE_FILT_WARN      1091
#define IDC_CHK_TRACE_FILT_INFO      1092
#define IDC_CHK_TRACE_FILT_VERBOSE   1093

// Feature tier & projection
#define IDC_COMBO_FEATURE_TIER       1080
#define IDC_STATIC_TIER_LABEL        1081
#define IDC_CHK_PROJECTION           1082
#define IDC_BTN_SHOW_MAPPING         1083
#define IDD_MAPPING                  103
#define IDC_BTN_SHOW_TIERS           1084
#define IDD_TIERS                    104
#define IDC_BTN_APPLY                1085
#define IDC_STATIC_PENDING_TEXT      1086
#define IDC_LINK_VERSION             1087
#define IDC_CHK_RESTART_EXPLORER     1088

#define IDC_BTN_TOGGLE_FILTER        1050
#define IDC_STATIC_FILTER_STATUS     1051
#define IDC_STATIC_FILTER_VER        1052

#define IDC_STATIC_SHP_LABEL         1028
#define IDC_STATIC_SHP_GITHUB        1029

#define IDC_BTN_FETCH_SHP            1053
#define IDC_BTN_REMOVE_SHP           1054
#define IDC_STATIC_SHP_LOCAL         1055
#define IDC_STATIC_SHP_MATCH         1056

#define IDC_STATIC_CST_LABEL         1057
#define IDC_STATIC_CST_GITHUB        1058
#define IDC_STATIC_CST_LOCAL         1059
#define IDC_STATIC_CST_MATCH         1046

#define IDC_BTN_FETCH_CST            1102
#define IDC_BTN_REMOVE_CST           1103

#define IDC_LIST_CATALOGS            1104
#define IDC_BTN_CATALOG_INSTALL      1105
#define IDC_BTN_CATALOG_REMOVE       1106
#define IDC_LINK_TIERS_DOC           1107

// Inline doc links + DLL path display (replace the legacy "open docs? Y/N"
// MessageBox prompts with persistent SysLink controls placed next to the
// relevant handler row / button.)
#define IDC_STATIC_PROPERTY_PATH     1108
#define IDC_STATIC_PREVIEW_PATH      1109
#define IDC_STATIC_FILTER_PATH       1110
#define IDC_LINK_PROPERTY_DOC        1111
#define IDC_LINK_PREVIEW_DOC         1112
#define IDC_LINK_FILTER_DOC          1113
#define IDC_LINK_ETW_DOC             1114
#define IDC_LINK_CATALOG_VERIFY_DOC  1115
#define IDC_LINK_ADVANCED_DOC        1116

// Software Update section
#define IDC_STATIC_UPDATE_INSTALLED  1120
#define IDC_STATIC_UPDATE_AVAILABLE  1121
#define IDC_STATIC_UPDATE_DETAIL     1122
#define IDC_BTN_CHECK_UPDATES        1123
#define IDC_BTN_INSTALL_UPDATE       1124

// Custom window messages for update background threads
#define WM_XISF_UPDATE_CHECK_DONE    (WM_APP + 20)
#define WM_XISF_UPDATE_DOWNLOAD_DONE (WM_APP + 21)
