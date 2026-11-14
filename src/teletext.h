#ifndef VORTEX_TELETEXT_H
#define VORTEX_TELETEXT_H

#include <stdint.h>
#include <stdbool.h>

#define TTX_COLS        40
#define TTX_ROWS        25
#define TTX_MAX_LINKS   64
#define TTX_CHANS_DE_COUNT    8
#define TTX_CHANS_INTL_COUNT  11
#define TTX_MAX_CHANS         (TTX_CHANS_DE_COUNT + TTX_CHANS_INTL_COUNT)
#define TTX_CACHE_SIZE  128
#define TTX_MAX_FAVS    32
#define TTX_HISTORY_MAX 32

/* Standard EBU ETS 300 706 colors */
enum TtxColor {
    TTX_BLACK   = 0,
    TTX_RED     = 1,
    TTX_GREEN   = 2,
    TTX_YELLOW  = 3,
    TTX_BLUE    = 4,
    TTX_MAGENTA = 5,
    TTX_CYAN    = 6,
    TTX_WHITE   = 7
};

/* Cell attribute bitmask */
#define TTX_ATTR_MOSAIC     (1 << 0)
#define TTX_ATTR_SEPARATED  (1 << 1)
#define TTX_ATTR_FLASH      (1 << 2)
#define TTX_ATTR_DOUBLE_H   (1 << 3)
#define TTX_ATTR_CONCEAL    (1 << 4)
#define TTX_ATTR_LINK       (1 << 5)

typedef enum {
    TTX_SRC_ARD = 0,
    TTX_SRC_ZDF = 1,
    TTX_SRC_HR  = 2,
    TTX_SRC_WDR = 3,
    TTX_SRC_SWR = 4,
    TTX_SRC_SR  = 5,
    TTX_SRC_SRF = 6,
    TTX_SRC_ORF = 7
} TtxSourceType;

typedef struct {
    const char *id;
    const char *name;
    const char *api_channel;
    TtxSourceType source;
} TtxChannel;

typedef struct {
    uint8_t  ch;       /* ISO-8859-1 char (0x20..0xFF) or 6-bit mosaic (0x00..0x3F when TTX_ATTR_MOSAIC) */
    uint8_t  fg;       /* Foreground color 0..7 */
    uint8_t  bg;       /* Background color 0..7 */
    uint8_t  attrs;    /* TTX_ATTR_* */
    uint16_t link_page;/* 0 if none, else 100..899 */
} TtxCell;

typedef struct {
    int  channel_idx;
    int  page;
    int  subpage;
    int  total_subpages;
    int  prev_page;
    int  next_page;
    int  red_page;
    int  green_page;
    int  yellow_page;
    int  cyan_page;
    char title[64];
    char timestamp[32];
    bool valid;
    TtxCell cells[TTX_ROWS][TTX_COLS];
} TtxPage;

typedef struct {
    int  channel_idx;
    int  page;
    char label[48];
} TtxFavorite;

typedef struct {
    int  channel_idx;
    int  page;
    int  subpage;
    char snippet[64];
} TtxSearchResult;

extern const TtxChannel g_ttx_channels[TTX_MAX_CHANS];

void ttx_page_init(TtxPage *pg, int channel_idx, int page, int subpage);
void ttx_page_detect_links(TtxPage *pg);

/* Parsers for live public teletext APIs */
bool ttx_parse_ard_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out);
bool ttx_parse_hr_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out);
bool ttx_parse_wdr_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out);

/* Live network fetch (uses http_client + AmiSSL) */
bool ttx_fetch_live_page(int channel_idx, int page, int subpage, TtxPage *out, char *err_buf, int err_len);

/* In-memory LRU page cache & full-text search */
void ttx_cache_init(void);
void ttx_cache_store(const TtxPage *pg);
bool ttx_cache_lookup(int channel_idx, int page, int subpage, TtxPage *out);
int  ttx_cache_search(const char *query, int channel_filter, TtxSearchResult *results, int max_results);
int  ttx_cache_count(void);

/* Export plain text */
bool ttx_export_text(const TtxPage *pg, const char *filepath);

#endif /* VORTEX_TELETEXT_H */
