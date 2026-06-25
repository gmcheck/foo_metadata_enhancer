#pragma once

#include <winres.h>

#ifndef IDAPPLY
#define IDAPPLY 0x3021
#endif

#ifndef IDVIEWDETAILS
#define IDVIEWDETAILS 0x3022
#endif

#ifndef IDREFRESH
#define IDREFRESH 0x3023
#endif

#define IDR_VERSION_INFO             1

#define IDD_PREFERENCES              100
#define IDD_PREF_GENERAL             108
#define IDD_PREF_DATA_SOURCES        109
#define IDD_PREF_ADVANCED            110
#define IDD_BATCH_SETTINGS           101
#define IDD_COMPLETION               102
#define IDD_ERROR                    103
#define IDD_CACHE_STATS              104
#define IDD_WORKER_STATUS            105
#define IDD_DETAILS                  106
#define IDD_ANALYSIS_OPTIONS         107

#define IDC_GROUP_BATCH              1001
#define IDC_BATCH_SIZE_LABEL         1002
#define IDC_BATCH_SIZE               1003
#define IDC_CONCURRENCY_LABEL        1004
#define IDC_CONCURRENCY              1005
#define IDC_ENABLE_CACHE             1006
#define IDC_SHOW_PROGRESS            1007
#define IDC_AUTO_CLEANUP             1008

#define IDC_GROUP_COMPLETION         1101
#define IDC_TOTAL_TRACKS             1102
#define IDC_SUCCESS_COUNT            1103
#define IDC_FAILED_COUNT             1104
#define IDC_CACHE_HITS               1105
#define IDC_API_CALLS                1106
#define IDC_ELAPSED_TIME             1107
#define IDC_TOKENS_USED              1108
#define IDC_DETAILS_LABEL            1109

#define IDC_GROUP_ERROR              1201
#define IDC_ERROR_TITLE              1202
#define IDC_ERROR_MESSAGE            1203
#define IDC_ERROR_DETAILS            1204
#define IDC_RETRY_BTN                1205
#define IDC_VIEW_LOG_BTN             1206
#define IDC_DETAILS_TEXT            1207

#define IDC_GROUP_CACHE_STATS        1301
#define IDC_CACHE_TOTAL_ENTRIES      1302
#define IDC_CACHE_TOTAL_HITS         1303
#define IDC_CACHE_HIT_RATE           1304
#define IDC_CACHE_STATS_SIZE         1305
#define IDC_CACHE_EXPIRED            1306
#define IDC_REFRESH_STATS_BTN        1307
#define IDC_EXPORT_STATS_BTN         1308
#define IDC_CLEAR_CACHE_BTN          1309

#define IDC_GROUP_WORKER_STATUS      1401
#define IDC_WORKER_LIST              1402
#define IDC_WORKER_DETAILS           1403
#define IDC_RESTART_WORKER_BTN       1404
#define IDC_RESTART_ALL_BTN          1405

#define IDC_GROUP_API                1500
#define IDC_PROVIDER_LABEL           1506
#define IDC_PROVIDER                 1507
#define IDC_API_KEY_LABEL            1501
#define IDC_API_KEY                  1502
#define IDC_MODEL_LABEL              1503
#define IDC_MODEL                    1504
#define IDC_USE_ENV_KEY              1505

#define IDC_GROUP_CACHE              1510
#define IDC_CACHE_EXPIRATION_LABEL   1511
#define IDC_CACHE_EXPIRATION         1512
#define IDC_CACHE_SIZE_LABEL         1513
#define IDC_CACHE_SIZE               1514
#define IDC_CLEAR_CACHE_BTN_PREF     1515

#define IDC_GROUP_WORKER             1520
#define IDC_POOL_SIZE_LABEL          1521
#define IDC_POOL_SIZE                1522
#define IDC_TIMEOUT_LABEL            1523
#define IDC_TIMEOUT                  1524
#define IDC_AUTO_RESTART             1525

#define IDC_GROUP_BATCH_PREF         1530
#define IDC_BATCH_SIZE_LABEL_PREF    1531
#define IDC_BATCH_SIZE_PREF          1532
#define IDC_CONCURRENCY_LABEL_PREF   1533
#define IDC_CONCURRENCY_PREF         1534
#define IDC_SHOW_PROGRESS_PREF       1535

#define IDC_GROUP_TASKQUEUE          1536
#define IDC_TASKQUEUE_BATCH_LABEL    1537
#define IDC_TASKQUEUE_BATCH_SIZE     1538

#define IDC_GROUP_MUSICBRAINZ        1600
#define IDC_GROUP_DISCOGS            1601
#define IDC_GROUP_AI                 1602
#define IDC_MB_TIMEOUT_LABEL         1603
#define IDC_MB_TIMEOUT               1604
#define IDC_MB_RETRIES_LABEL         1605
#define IDC_MB_RETRIES               1606
#define IDC_MB_PAGE_SIZE_LABEL       1607
#define IDC_MB_PAGE_SIZE             1608
#define IDC_MB_MAX_PAGES_LABEL       1609
#define IDC_MB_MAX_PAGES             1610
#define IDC_MB_SCORE_THRESHOLD_LABEL 1611
#define IDC_MB_SCORE_THRESHOLD       1612
#define IDC_MB_SCORE_MARGIN_LABEL    1613
#define IDC_MB_SCORE_MARGIN          1614
#define IDC_MB_RATE_LIMIT_LABEL      1615
#define IDC_MB_RATE_LIMIT            1616

#define IDC_GROUP_LOGGING            1540
#define IDC_LOG_LEVEL_LABEL          1541
#define IDC_LOG_LEVEL                1542
#define IDC_LOG_SIZE_LABEL           1543
#define IDC_LOG_SIZE                 1544
#define IDC_OPEN_LOG_BTN             1545

#define IDC_GROUP_MENU               1550
#define IDC_MENU_ANALYZE_SELECTED    1551
#define IDC_MENU_ANALYZE_ALL         1552
#define IDC_MENU_BATCH_SETTINGS      1553
#define IDC_MENU_CACHE_STATS         1554
#define IDC_MENU_ROLLBACK            1555
#define IDC_MENU_CLEAR_CACHE         1556
#define IDC_MENU_SETTINGS            1557

#define IDC_TEST_API_BTN             1560
#define IDC_STATUS_TEXT              1561
#define IDC_RESTART_WORKERS_BTN      1562
#define IDC_WORKER_STATUS_TEXT       1563

#define IDC_PER_TRACK_TIMEOUT_LABEL  1564
#define IDC_PER_TRACK_TIMEOUT        1565

#define IDC_GROUP_PYTHON             1570
#define IDC_PYTHON_PATH_LABEL        1571
#define IDC_PYTHON_PATH              1572
#define IDC_PYTHON_BROWSE            1573
#define IDC_AUTO_INSTALL_PACKAGES    1574
#define IDC_PYTHON_STATUS            1575

#define IDC_GROUP_ANALYSIS_OPTIONS   1600
#define IDC_OPT_CLASSIFY_GENRE       1601
#define IDC_OPT_IDENTIFY_EDITION     1602
#define IDC_OPT_TRANSLATE_METADATA   1603

#define IDD_SCRAPING_OPTIONS         3000
#define IDD_ENHANCEMENT_OPTIONS      3010
#define IDD_CONFIRM_RESULT           3020
#define IDD_EDIT_FIELD               3040
#define IDD_CONFIRM_ENHANCEMENT      3050
#define IDD_CLEAR_CACHE              3060

#define IDC_RESULT_LISTVIEW          3100
#define IDC_ENHANCE_LISTVIEW         3101

#define IDC_CLEAR_ALL_CACHE          3110

#define IDC_SCRAPE_TITLE             3121
#define IDC_SCRAPE_ARTIST            3122
#define IDC_SCRAPE_ALBUM             3123
#define IDC_SCRAPE_YEAR              3124
#define IDC_SCRAPE_TRACK_NUMBER      3125
#define IDC_SCRAPE_DISC_NUMBER       3126
#define IDC_SCRAPE_COMPOSER          3127
#define IDC_SCRAPE_LYRICIST          3128
#define IDC_SCRAPE_CONDUCTOR         3129
#define IDC_SCRAPE_PERFORMER         3130
#define IDC_SCRAPE_LABEL             3131
#define IDC_ENABLE_MUSICBRAINZ       3132
#define IDC_ENABLE_DISCOGS           3133
#define IDC_ENABLE_AI                3134
#define IDC_AUTO_ACCEPT_THRESHOLD    3135
#define IDC_CONFIRM_THRESHOLD        3136

#define IDC_ENHANCE_TRANSLATE_TITLE  3141
#define IDC_ENHANCE_TRANSLATE_ALBUM  3142
#define IDC_ENHANCE_TRANSLATE_ARTIST 3143
#define IDC_ENHANCE_CLASSIFY_GENRE   3144
#define IDC_ENHANCE_IDENTIFY_EDITION 3145

#define IDC_SELECT_ALL               3151
#define IDC_SELECT_NONE              3152
#define IDC_EDIT_ITEM                3153
#define IDC_SELECT_SUCCESS           3154

#define IDC_FIELD_TITLE              3160
#define IDC_FIELD_ARTIST             3161
#define IDC_FIELD_ALBUM              3162
#define IDC_FIELD_YEAR               3163
#define IDC_FIELD_TRACK_NUMBER       3164
#define IDC_FIELD_DISC_NUMBER        3165
#define IDC_FIELD_COMPOSER           3166
#define IDC_FIELD_LYRICIST           3167
#define IDC_FIELD_CONDUCTOR          3168
#define IDC_FIELD_PERFORMER          3169
#define IDC_FIELD_LABEL              3170
#define IDC_FIELD_GENRE              3171

#define IDC_EDIT_FIELD_COMBO         3181
#define IDC_EDIT_VALUE               3172
#define IDC_EDIT_CONFIDENCE          3173
#define IDC_EDIT_SOURCE              3174
#define IDC_EDIT_ALL_FIELDS          3175
#define IDC_EDIT_ORIGINAL            3176
#define IDC_EDIT_SCRAPED             3177
#define IDC_RESTORE_ORIGINAL         3178
#define IDC_RESTORE_SCRAPED          3179
#define IDC_EDIT_EXISTING            3180

#define IDC_ENHANCE_FIELD_TITLE_ZH   3190
#define IDC_ENHANCE_FIELD_ALBUM_ZH   3191
#define IDC_ENHANCE_FIELD_ARTIST_ZH  3192
#define IDC_ENHANCE_FIELD_GENRE      3193
#define IDC_ENHANCE_FIELD_EDITION    3194

#define IDI_ICON                     3001
#define IDB_LOGO                     3002
