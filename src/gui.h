#ifndef VORTEX_GUI_H
#define VORTEX_GUI_H

#include "teletext.h"
#include "ttx_render.h"
#include "prefs.h"

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

enum {
    GID_CANVAS = 1,
    GID_KEY_0,
    GID_KEY_1,
    GID_KEY_2,
    GID_KEY_3,
    GID_KEY_4,
    GID_KEY_5,
    GID_KEY_6,
    GID_KEY_7,
    GID_KEY_8,
    GID_KEY_9,
    GID_KEY_DISPLAY,
    GID_BTN_GO,
    GID_BTN_PREV_PAGE,
    GID_BTN_NEXT_PAGE,
    GID_BTN_PREV_SUB,
    GID_BTN_NEXT_SUB,
    GID_STR_PAGE,
    GID_BTN_RELOAD,
    GID_CHOOSER_CHAN,
    GID_MAX
};

enum {
    MID_PROJ_HOME = 100,
    MID_PROJ_RELOAD,
    MID_PROJ_EXPORT_TXT,
    MID_PROJ_EXPORT_BMP,
    MID_PROJ_ABOUT,
    MID_PROJ_QUIT,
    MID_NAV_BACK,
    MID_NAV_PREV_PAGE,
    MID_NAV_NEXT_PAGE,
    MID_NAV_PREV_SUB,
    MID_NAV_NEXT_SUB,
    MID_FUNC_SEARCH,
    MID_FUNC_FAVS,
    MID_FUNC_REVEAL,
    MID_FUNC_FLASH,
    MID_FUNC_FIT_WIDTH,
    MID_FUNC_ZOOM_RESET
};

typedef struct {
    Object          *win_obj;
    struct Window   *win;
    Object          *gadgets[GID_MAX];
    struct List      chan_list;

    Object          *fav_win_obj;
    struct Window   *fav_win;
    Object          *fav_lb;
    struct List      fav_list;

    TtxPage          cur_page;
    TtxRenderer      renderer;
    TtxPrefs         prefs;

    uint32_t        *scaled_pixels;
    int              scaled_cap;
    struct BitMap   *src_bm;
    struct BitMap   *comp_bm;
    int              comp_w;
    int              comp_h;
    bool             comp_dirty;

    TtxFavorite      favs[TTX_MAX_FAVS];
    int              fav_count;

    int              hist_chan[TTX_HISTORY_MAX];
    int              hist_page[TTX_HISTORY_MAX];
    int              hist_len;

    char             num_buf[4];
    int              num_len;
    int              tick_count;
    int              zoom_pct;
    int              view_x;
    int              view_y;
    int              wheel_delta;
    int              wheel_mx;
    int              wheel_my;
    char             status_msg[128];
    char             win_title[160];
    bool             running;
} TtxApp;

bool gui_init(TtxApp *app);
void gui_cleanup(TtxApp *app);
void gui_run_loop(TtxApp *app);
void gui_load_page(TtxApp *app, int channel_idx, int page, int subpage,
                   bool push_history, bool force_reload);

#endif /* VORTEX_GUI_H */
