#include "gui.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/requester.h>

#include <classes/window.h>
#include <classes/requester.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/space.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/bevel.h>
#include <libraries/gadtools.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <utility/hooks.h>

static struct NewMenu s_menu[] = {
    { NM_TITLE, (STRPTR)"Projekt",              NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Startseite (100)",     (STRPTR)"1", 0, 0, (APTR)MID_PROJ_HOME },
    { NM_ITEM,  (STRPTR)"Aktualisieren",        (STRPTR)"R", 0, 0, (APTR)MID_PROJ_RELOAD },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Als Text exportieren", (STRPTR)"T", 0, 0, (APTR)MID_PROJ_EXPORT_TXT },
    { NM_ITEM,  (STRPTR)"Als BMP exportieren",  (STRPTR)"S", 0, 0, (APTR)MID_PROJ_EXPORT_BMP },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Ueber VortexTeletext", NULL,        0, 0, (APTR)MID_PROJ_ABOUT },
    { NM_ITEM,  (STRPTR)"Beenden",              (STRPTR)"Q", 0, 0, (APTR)MID_PROJ_QUIT },

    { NM_TITLE, (STRPTR)"Navigation",           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Zurueck (Verlauf)",    NULL,        0, 0, (APTR)MID_NAV_BACK },
    { NM_ITEM,  (STRPTR)"Vorige Seite",         NULL,        0, 0, (APTR)MID_NAV_PREV_PAGE },
    { NM_ITEM,  (STRPTR)"Naechste Seite",       NULL,        0, 0, (APTR)MID_NAV_NEXT_PAGE },
    { NM_ITEM,  (STRPTR)"Vorige Unterseite",    NULL,        0, 0, (APTR)MID_NAV_PREV_SUB },
    { NM_ITEM,  (STRPTR)"Naechste Unterseite",  NULL,        0, 0, (APTR)MID_NAV_NEXT_SUB },

    { NM_TITLE, (STRPTR)"Funktionen",           NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"Volltextsuche...",     (STRPTR)"F", 0, 0, (APTR)MID_FUNC_SEARCH },
    { NM_ITEM,  (STRPTR)"Favoriten...",         (STRPTR)"B", 0, 0, (APTR)MID_FUNC_FAVS },
    { NM_ITEM,  (STRPTR)"Verdeckten Text (Reveal)", (STRPTR)"H", 0, 0, (APTR)MID_FUNC_REVEAL },
    { NM_ITEM,  (STRPTR)"Blinken umschalten",   NULL,        0, 0, (APTR)MID_FUNC_FLASH },
    { NM_ITEM,  NM_BARLABEL,                    NULL,        0, 0, NULL },
    { NM_ITEM,  (STRPTR)"An Fensterbreite anpassen", NULL, CHECKIT | MENUTOGGLE | CHECKED, 0, (APTR)MID_FUNC_FIT_WIDTH },
    { NM_ITEM,  (STRPTR)"Zoom zuruecksetzen (100%)", (STRPTR)"0", 0, 0, (APTR)MID_FUNC_ZOOM_RESET },

    { NM_END, NULL, NULL, 0, 0, NULL }
};

static void show_info_requester(struct Window *win, const char *title, const char *body) {
    Object *req = IIntuition->NewObject(NULL, "requester.class",
        REQ_Type,       REQTYPE_INFO,
        REQ_TitleText,  (ULONG)title,
        REQ_BodyText,   (ULONG)body,
        REQ_GadgetText, (ULONG)"_OK",
        TAG_DONE);
    if (req) {
        IIntuition->IDoMethod(req, RM_OPENREQ, NULL, win, NULL, TAG_DONE);
        IIntuition->DisposeObject(req);
    }
}

static void get_canvas_draw_rect(TtxApp *app, int *out_x, int *out_y, int *out_w, int *out_h,
                                 struct IBox *out_box) {
    struct IBox box;
    memset(&box, 0, sizeof(box));
    if (app->gadgets[GID_CANVAS]) {
        struct IBox *ib = NULL;
        IIntuition->GetAttr(SPACE_AreaBox, app->gadgets[GID_CANVAS], (ULONG *)&ib);
        if (ib) box = *ib;
    }
    if (out_box) *out_box = box;

    if (box.Width < 16 || box.Height < 16) {
        *out_x = box.Left;
        *out_y = box.Top;
        *out_w = 0;
        *out_h = 0;
        return;
    }

    if (app->prefs.fit_width) {
        *out_x = box.Left;
        *out_y = box.Top;
        *out_w = box.Width;
        *out_h = box.Height;
        return;
    }

    /* Fit 640:500 aspect ratio inside box */
    int draw_w = box.Width;
    int draw_h = (draw_w * TTX_FB_H) / TTX_FB_W;
    if (draw_h > box.Height) {
        draw_h = box.Height;
        draw_w = (draw_h * TTX_FB_W) / TTX_FB_H;
    }
    if (draw_w < 16) draw_w = 16;
    if (draw_h < 16) draw_h = 16;

    *out_x = box.Left + (box.Width - draw_w) / 2;
    *out_y = box.Top + (box.Height - draw_h) / 2;
    *out_w = draw_w;
    *out_h = draw_h;
}

static void get_zoom_src_rect(TtxApp *app, int *out_sx, int *out_sy, int *out_sw, int *out_sh) {
    int zp = app->zoom_pct;
    if (zp < 100) zp = 100;
    if (zp > 300) zp = 300;
    int sw = (TTX_FB_W * 100) / zp;
    int sh = (TTX_FB_H * 100) / zp;
    if (sw < 64) sw = 64;
    if (sh < 50) sh = 50;
    if (sw > TTX_FB_W) sw = TTX_FB_W;
    if (sh > TTX_FB_H) sh = TTX_FB_H;

    int max_x = TTX_FB_W - sw;
    int max_y = TTX_FB_H - sh;
    if (app->view_x < 0) app->view_x = 0;
    if (app->view_y < 0) app->view_y = 0;
    if (app->view_x > max_x) app->view_x = max_x;
    if (app->view_y > max_y) app->view_y = max_y;

    *out_sx = app->view_x;
    *out_sy = app->view_y;
    *out_sw = sw;
    *out_sh = sh;
}

static void scale_framebuffer_rows(const uint32_t *src, int src_stride,
                                   int sx, int sy, int sw, int sh,
                                   uint32_t *dst, int dw, int dh,
                                   int dy_start, int rows) {
    if (dw < sw) {
        /* 4-tap bilinear downscale for crisp thin strokes */
        uint32_t x_step = ((uint32_t)(sw - 1) << 16) / (uint32_t)(dw > 1 ? dw - 1 : 1);
        uint32_t y_step = ((uint32_t)(sh - 1) << 16) / (uint32_t)(dh > 1 ? dh - 1 : 1);
        uint32_t sy_fp = (uint32_t)dy_start * y_step;
        for (int y = 0; y < rows; y++) {
            int y0 = sy + (int)(sy_fp >> 16);
            if (y0 >= sy + sh) y0 = sy + sh - 1;
            int y1 = (y0 + 1 < sy + sh) ? y0 + 1 : y0;
            uint32_t fy = (sy_fp & 0xFFFF) >> 8;
            uint32_t iy = 256 - fy;
            const uint32_t *r0 = src + y0 * src_stride;
            const uint32_t *r1 = src + y1 * src_stride;
            uint32_t *out_row = dst + y * dw;
            uint32_t sx_fp = 0;
            for (int x = 0; x < dw; x++) {
                int x0 = sx + (int)(sx_fp >> 16);
                if (x0 >= sx + sw) x0 = sx + sw - 1;
                int x1 = (x0 + 1 < sx + sw) ? x0 + 1 : x0;
                uint32_t fx = (sx_fp & 0xFFFF) >> 8;
                uint32_t ix = 256 - fx;

                uint32_t p00 = r0[x0], p10 = r0[x1];
                uint32_t p01 = r1[x0], p11 = r1[x1];

                uint32_t w00 = ix * iy;
                uint32_t w10 = fx * iy;
                uint32_t w01 = ix * fy;
                uint32_t w11 = fx * fy;

                uint32_t rr = (((p00 >> 16) & 0xFF) * w00 + ((p10 >> 16) & 0xFF) * w10 +
                               ((p01 >> 16) & 0xFF) * w01 + ((p11 >> 16) & 0xFF) * w11) >> 16;
                uint32_t gg = (((p00 >> 8)  & 0xFF) * w00 + ((p10 >> 8)  & 0xFF) * w10 +
                               ((p01 >> 8)  & 0xFF) * w01 + ((p11 >> 8)  & 0xFF) * w11) >> 16;
                uint32_t bb = ((p00 & 0xFF) * w00 + (p10 & 0xFF) * w10 +
                               (p01 & 0xFF) * w01 + (p11 & 0xFF) * w11) >> 16;

                out_row[x] = 0xFF000000u | (rr << 16) | (gg << 8) | bb;
                sx_fp += x_step;
            }
            sy_fp += y_step;
        }
    } else {
        /* Crisp nearest-neighbor scale */
        for (int y = 0; y < rows; y++) {
            int ry = sy + ((dy_start + y) * sh) / dh;
            if (ry >= sy + sh) ry = sy + sh - 1;
            const uint32_t *srow = src + ry * src_stride;
            uint32_t *drow = dst + y * dw;
            for (int x = 0; x < dw; x++) {
                int rx = sx + (x * sw) / dw;
                if (rx >= sx + sw) rx = sx + sw - 1;
                drow[x] = srow[rx];
            }
        }
    }
}

static void gui_redraw_canvas_rp(TtxApp *app, struct RastPort *rp) {
    if (!app || !rp || !app->gadgets[GID_CANVAS]) return;
    struct IBox box;
    int off_x, off_y, draw_w, draw_h;
    get_canvas_draw_rect(app, &off_x, &off_y, &draw_w, &draw_h, &box);
    if (draw_w < 16 || draw_h < 16) return;

    /* Clear border around centered teletext screen if larger */
    if (box.Width > draw_w || box.Height > draw_h) {
        IGraphics->SetAPen(rp, 0);
        if (off_y > box.Top)
            IGraphics->RectFill(rp, box.Left, box.Top, box.Left + box.Width - 1, off_y - 1);
        if (off_y + draw_h < box.Top + box.Height)
            IGraphics->RectFill(rp, box.Left, off_y + draw_h, box.Left + box.Width - 1, box.Top + box.Height - 1);
        if (off_x > box.Left)
            IGraphics->RectFill(rp, box.Left, off_y, off_x - 1, off_y + draw_h - 1);
        if (off_x + draw_w < box.Left + box.Width)
            IGraphics->RectFill(rp, off_x + draw_w, off_y, box.Left + box.Width - 1, off_y + draw_h - 1);
    }

    int sx, sy, sw, sh;
    get_zoom_src_rect(app, &sx, &sy, &sw, &sh);

    if (sx == 0 && sy == 0 && sw == TTX_FB_W && sh == TTX_FB_H &&
        draw_w == TTX_FB_W && draw_h == TTX_FB_H) {
        IGraphics->WritePixelArray(
            (APTR)app->renderer.pixels,
            0, 0,
            TTX_FB_W * 4,
            PIXF_A8R8G8B8,
            rp,
            off_x, off_y,
            draw_w, draw_h
        );
    } else {
        if (!app->scaled_pixels || app->scaled_cap < draw_w) return;
        int strip_h = app->scaled_cap / draw_w;
        if (strip_h < 1) strip_h = 1;

        if (app->render_mutex) IExec->MutexObtain(app->render_mutex);
        for (int dy = 0; dy < draw_h; dy += strip_h) {
            int ch = draw_h - dy;
            if (ch > strip_h) ch = strip_h;
            scale_framebuffer_rows(app->renderer.pixels, TTX_FB_W,
                                   sx, sy, sw, sh,
                                   app->scaled_pixels, draw_w, draw_h,
                                   dy, ch);
            IGraphics->WritePixelArray(
                (APTR)app->scaled_pixels,
                0, 0,
                draw_w * 4,
                PIXF_A8R8G8B8,
                rp,
                off_x, off_y + dy,
                draw_w, ch
            );
        }
        if (app->render_mutex) IExec->MutexRelease(app->render_mutex);
    }
}

static void gui_redraw_canvas(TtxApp *app) {
    if (!app->win) return;
    gui_redraw_canvas_rp(app, app->win->RPort);
}

static bool canvas_hit_test(TtxApp *app, int mx, int my, int *out_row, int *out_col) {
    int off_x, off_y, draw_w, draw_h;
    get_canvas_draw_rect(app, &off_x, &off_y, &draw_w, &draw_h, NULL);
    if (draw_w <= 0 || draw_h <= 0) return false;
    int rx = mx - off_x;
    int ry = my - off_y;
    if (rx < 0 || ry < 0 || rx >= draw_w || ry >= draw_h) return false;

    int sx, sy, sw, sh;
    get_zoom_src_rect(app, &sx, &sy, &sw, &sh);
    int px = sx + (rx * sw) / draw_w;
    int py = sy + (ry * sh) / draw_h;
    return ttx_render_hit_test(px, py, TTX_FB_W, TTX_FB_H, out_row, out_col);
}

static void gui_update_status(TtxApp *app) {
    if (!app->win) return;
    char zbuf[24] = "";
    if (app->zoom_pct > 100) {
        snprintf(zbuf, sizeof(zbuf), " [Zoom %d%%]", app->zoom_pct);
    }
    if (app->renderer.hover_link >= 100) {
        snprintf(app->win_title, sizeof(app->win_title),
                 "VortexTeletext - %s %d (%d/%d)%s | Klick -> Seite %d",
                 g_ttx_channels[app->cur_page.channel_idx].name,
                 app->cur_page.page, app->cur_page.subpage, app->cur_page.total_subpages,
                 zbuf, app->renderer.hover_link);
    } else if (app->status_msg[0]) {
        snprintf(app->win_title, sizeof(app->win_title),
                 "VortexTeletext - %s", app->status_msg);
    } else {
        snprintf(app->win_title, sizeof(app->win_title),
                 "VortexTeletext - %s %d (%d/%d)%s",
                 g_ttx_channels[app->cur_page.channel_idx].name,
                 app->cur_page.page, app->cur_page.subpage, app->cur_page.total_subpages,
                 zbuf);
    }
    IIntuition->SetWindowTitles(app->win, (STRPTR)app->win_title, (STRPTR)-1);
}

static void apply_mouse_wheel_zoom(TtxApp *app, int steps, int mx, int my) {
    if (!app->win || steps == 0) return;
    int off_x, off_y, draw_w, draw_h;
    get_canvas_draw_rect(app, &off_x, &off_y, &draw_w, &draw_h, NULL);
    if (draw_w <= 0 || draw_h <= 0) return;
    if (mx < off_x || my < off_y || mx >= off_x + draw_w || my >= off_y + draw_h) return;

    int old_sx, old_sy, old_sw, old_sh;
    get_zoom_src_rect(app, &old_sx, &old_sy, &old_sw, &old_sh);

    int rx = mx - off_x;
    int ry = my - off_y;
    int focus_x = old_sx + (rx * old_sw) / draw_w;
    int focus_y = old_sy + (ry * old_sh) / draw_h;

    int new_zp = app->zoom_pct + steps * 10;
    if (new_zp < 100) new_zp = 100;
    if (new_zp > 300) new_zp = 300;
    if (new_zp == app->zoom_pct) return;

    app->zoom_pct = new_zp;
    int new_sw = (TTX_FB_W * 100) / new_zp;
    int new_sh = (TTX_FB_H * 100) / new_zp;
    app->view_x = focus_x - (rx * new_sw) / draw_w;
    app->view_y = focus_y - (ry * new_sh) / draw_h;

    int sx, sy, sw, sh;
    get_zoom_src_rect(app, &sx, &sy, &sw, &sh);
    gui_redraw_canvas(app);
    gui_update_status(app);
}

static void gui_update_keypad_display(TtxApp *app, const char *txt) {
    if (!app->win || !app->gadgets[GID_KEY_DISPLAY]) return;
    IIntuition->SetGadgetAttrs((struct Gadget *)app->gadgets[GID_KEY_DISPLAY], app->win, NULL,
                               GA_Text, (ULONG)txt, TAG_DONE);
}

static void gui_sync_controls(TtxApp *app) {
    if (!app->win) return;
    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%03d", app->cur_page.page);
    IIntuition->SetGadgetAttrs((struct Gadget *)app->gadgets[GID_STR_PAGE], app->win, NULL,
                               STRINGA_TextVal, (ULONG)pbuf, TAG_DONE);
    IIntuition->SetGadgetAttrs((struct Gadget *)app->gadgets[GID_CHOOSER_CHAN], app->win, NULL,
                               CHOOSER_Selected, (ULONG)app->cur_page.channel_idx, TAG_DONE);
    gui_update_keypad_display(app, app->num_len > 0 ? app->num_buf : "");
}

void gui_load_page(TtxApp *app, int channel_idx, int page, int subpage,
                   bool push_history, bool force_reload) {
    if (channel_idx < 0 || channel_idx >= TTX_MAX_CHANS) channel_idx = 0;
    if (page < 100 || page > 899) page = 100;
    if (subpage < 1) subpage = 1;

    if (push_history && app->cur_page.valid) {
        if (app->cur_page.channel_idx != channel_idx || app->cur_page.page != page) {
            if (app->hist_len < TTX_HISTORY_MAX) {
                app->hist_chan[app->hist_len] = app->cur_page.channel_idx;
                app->hist_page[app->hist_len] = app->cur_page.page;
                app->hist_len++;
            } else {
                memmove(&app->hist_chan[0], &app->hist_chan[1], sizeof(int) * (TTX_HISTORY_MAX - 1));
                memmove(&app->hist_page[0], &app->hist_page[1], sizeof(int) * (TTX_HISTORY_MAX - 1));
                app->hist_chan[TTX_HISTORY_MAX - 1] = app->cur_page.channel_idx;
                app->hist_page[TTX_HISTORY_MAX - 1] = app->cur_page.page;
            }
        }
    }

    TtxPage loaded;
    if (!force_reload && ttx_cache_lookup(channel_idx, page, subpage, &loaded)) {
        app->cur_page = loaded;
        app->status_msg[0] = '\0';
        app->prefs.default_channel = channel_idx;
        app->prefs.default_page = page;
    } else {
        snprintf(app->status_msg, sizeof(app->status_msg), "Lade %s Seite %d.%d ...",
                 g_ttx_channels[channel_idx].name, page, subpage);
        gui_update_status(app);

        char err[128] = {0};
        if (ttx_fetch_live_page(channel_idx, page, subpage, &loaded, err, sizeof(err))) {
            app->cur_page = loaded;
            app->status_msg[0] = '\0';
            app->prefs.default_channel = channel_idx;
            app->prefs.default_page = page;
        } else if (ttx_cache_lookup(channel_idx, page, subpage, &loaded)) {
            app->cur_page = loaded;
            snprintf(app->status_msg, sizeof(app->status_msg), "Offline-Cache (%s)", err);
        } else {
            snprintf(app->status_msg, sizeof(app->status_msg), "Fehler: %s", err[0] ? err : "Unbekannt");
        }
    }

    app->renderer.hover_link = 0;
    ttx_render_page(&app->renderer, &app->cur_page);
    gui_sync_controls(app);
    gui_redraw_canvas(app);
    gui_update_status(app);
}

static void gui_history_back(TtxApp *app) {
    if (app->hist_len <= 0) return;
    app->hist_len--;
    int ch = app->hist_chan[app->hist_len];
    int pg = app->hist_page[app->hist_len];
    gui_load_page(app, ch, pg, 1, false, false);
}

static void gui_export_current(TtxApp *app, bool as_bmp) {
    char path[320];
    snprintf(path, sizeof(path), "%s%sTeletext_%s_%d.%s",
             app->prefs.export_dir,
             (app->prefs.export_dir[strlen(app->prefs.export_dir) - 1] == ':' ||
              app->prefs.export_dir[strlen(app->prefs.export_dir) - 1] == '/') ? "" : "/",
             g_ttx_channels[app->cur_page.channel_idx].id,
             app->cur_page.page,
             as_bmp ? "bmp" : "txt");

    bool ok = as_bmp ? ttx_export_bmp(&app->renderer, path)
                     : ttx_export_text(&app->cur_page, path);
    if (ok) {
        snprintf(app->status_msg, sizeof(app->status_msg), "Exportiert: %s", path);
    } else {
        snprintf(app->status_msg, sizeof(app->status_msg), "Export fehlgeschlagen: %s", path);
    }
    gui_update_status(app);
}

/* ============================================================================
 * Full-Text Search Dialog (across cached pages)
 * ========================================================================== */

static void open_search_dialog(TtxApp *app) {
    struct List res_list;
    IExec->NewList(&res_list);

    Object *str_query = NULL, *btn_go = NULL, *lb_res = NULL, *btn_open = NULL, *btn_close = NULL;
    Object *swin_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,          (ULONG)"VortexTeletext - Volltextsuche (Cache)",
        WA_Width,          480,
        WA_Height,         320,
        WA_DragBar,        TRUE,
        WA_DepthGadget,    TRUE,
        WA_CloseGadget,    TRUE,
        WA_Activate,       TRUE,
        WINDOW_Position,   WPOS_CENTERWINDOW,
        WINDOW_RefWindow,  (ULONG)app->win,
        WINDOW_Layout, IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,  TRUE,
            LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, str_query = IIntuition->NewObject(NULL, "string.gadget",
                    GA_ID, 1, GA_RelVerify, TRUE, STRINGA_MaxChars, 64, TAG_DONE),
                LAYOUT_AddChild, btn_go = IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 2, GA_RelVerify, TRUE, GA_Text, (ULONG)"Suchen", TAG_DONE),
                CHILD_WeightedWidth, 25,
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, lb_res = IIntuition->NewObject(NULL, "listbrowser.gadget",
                GA_ID, 3, GA_RelVerify, TRUE,
                LISTBROWSER_Labels, (ULONG)&res_list,
                LISTBROWSER_ShowSelected, TRUE,
                TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, btn_open = IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 4, GA_RelVerify, TRUE, GA_Text, (ULONG)"Seite oeffnen", TAG_DONE),
                LAYOUT_AddChild, btn_close = IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 5, GA_RelVerify, TRUE, GA_Text, (ULONG)"Schliessen", TAG_DONE),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);

    if (!swin_obj) return;
    struct Window *swin = (struct Window *)IIntuition->IDoMethod(swin_obj, WM_OPEN, NULL);
    if (!swin) {
        IIntuition->DisposeObject(swin_obj);
        return;
    }

    TtxSearchResult results[32];
    int res_count = 0;
    bool done = false;
    ULONG sigmask = 0;
    IIntuition->GetAttr(WINDOW_SigMask, swin_obj, &sigmask);

    while (!done) {
        IExec->Wait(sigmask);
        ULONG result;
        WORD code;
        while ((result = IIntuition->IDoMethod(swin_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    done = true;
                    break;
                case WMHI_GADGETUP: {
                    ULONG gid = result & WMHI_GADGETMASK;
                    if (gid == 1 || gid == 2) {
                        STRPTR q = NULL;
                        IIntuition->GetAttr(STRINGA_TextVal, str_query, (ULONG *)&q);
                        IIntuition->SetGadgetAttrs((struct Gadget *)lb_res, swin, NULL,
                                                   LISTBROWSER_Labels, ~0, TAG_DONE);
                        IListBrowser->FreeListBrowserList(&res_list);
                        IExec->NewList(&res_list);
                        res_count = ttx_cache_search(q ? (const char *)q : "", -1, results, 32);
                        for (int i = 0; i < res_count; i++) {
                            char row_txt[96];
                            snprintf(row_txt, sizeof(row_txt), "[%s %d] %s",
                                     g_ttx_channels[results[i].channel_idx].name,
                                     results[i].page, results[i].snippet);
                            struct Node *n = IListBrowser->AllocListBrowserNode(1,
                                LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)row_txt,
                                TAG_DONE);
                            if (n) IExec->AddTail(&res_list, n);
                        }
                        IIntuition->SetGadgetAttrs((struct Gadget *)lb_res, swin, NULL,
                                                   LISTBROWSER_Labels, (ULONG)&res_list, TAG_DONE);
                    } else if (gid == 4) {
                        LONG sel = -1;
                        IIntuition->GetAttr(LISTBROWSER_Selected, lb_res, (ULONG *)&sel);
                        if (sel >= 0 && sel < res_count) {
                            gui_load_page(app, results[sel].channel_idx, results[sel].page, results[sel].subpage, true, false);
                            done = true;
                        }
                    } else if (gid == 5) {
                        done = true;
                    }
                    break;
                }
            }
        }
    }

    IIntuition->IDoMethod(swin_obj, WM_CLOSE, NULL);
    IIntuition->DisposeObject(swin_obj);
    IListBrowser->FreeListBrowserList(&res_list);
}

/* ============================================================================
 * Favorites Dialog
 * ========================================================================== */

static void rebuild_fav_list(struct List *list, const TtxFavorite *favs, int count) {
    IListBrowser->FreeListBrowserList(list);
    IExec->NewList(list);
    for (int i = 0; i < count; i++) {
        char txt[80];
        snprintf(txt, sizeof(txt), "[%s %d] %s",
                 g_ttx_channels[favs[i].channel_idx].name, favs[i].page, favs[i].label);
        struct Node *n = IListBrowser->AllocListBrowserNode(1,
            LBNA_Column, 0, LBNCA_CopyText, TRUE, LBNCA_Text, (ULONG)txt, TAG_DONE);
        if (n) IExec->AddTail(list, n);
    }
}

static void close_favorites_dialog(TtxApp *app) {
    if (app->fav_win_obj) {
        IIntuition->IDoMethod(app->fav_win_obj, WM_CLOSE, NULL);
        IIntuition->DisposeObject(app->fav_win_obj);
        app->fav_win_obj = NULL;
        app->fav_win = NULL;
        app->fav_lb = NULL;
        IListBrowser->FreeListBrowserList(&app->fav_list);
        IExec->NewList(&app->fav_list);
    }
}

static void open_favorites_dialog(TtxApp *app) {
    if (app->fav_win_obj && app->fav_win) {
        IIntuition->WindowToFront(app->fav_win);
        IIntuition->ActivateWindow(app->fav_win);
        return;
    }

    IExec->NewList(&app->fav_list);
    rebuild_fav_list(&app->fav_list, app->favs, app->fav_count);

    app->fav_win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,         (ULONG)"VortexTeletext - Favoriten",
        WA_Width,         420,
        WA_Height,        280,
        WA_DragBar,       TRUE,
        WA_DepthGadget,   TRUE,
        WA_SizeGadget,    TRUE,
        WA_CloseGadget,   TRUE,
        WA_Activate,      TRUE,
        WINDOW_Position,  WPOS_CENTERWINDOW,
        WINDOW_RefWindow, (ULONG)app->win,
        WINDOW_Layout, IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,  TRUE,
            LAYOUT_AddChild, app->fav_lb = IIntuition->NewObject(NULL, "listbrowser.gadget",
                GA_ID, 1, GA_RelVerify, TRUE,
                LISTBROWSER_Labels, (ULONG)&app->fav_list,
                LISTBROWSER_ShowSelected, TRUE,
                TAG_DONE),
            LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 2, GA_RelVerify, TRUE, GA_Text, (ULONG)"Oeffnen", TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 3, GA_RelVerify, TRUE, GA_Text, (ULONG)"+ Aktuelle", TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 4, GA_RelVerify, TRUE, GA_Text, (ULONG)"Loeschen", TAG_DONE),
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "button.gadget",
                    GA_ID, 5, GA_RelVerify, TRUE, GA_Text, (ULONG)"Schliessen", TAG_DONE),
                TAG_DONE),
            CHILD_WeightedHeight, 0,
            TAG_DONE),
        TAG_DONE);

    if (!app->fav_win_obj) {
        IListBrowser->FreeListBrowserList(&app->fav_list);
        IExec->NewList(&app->fav_list);
        return;
    }
    app->fav_win = (struct Window *)IIntuition->IDoMethod(app->fav_win_obj, WM_OPEN, NULL);
    if (!app->fav_win) {
        IIntuition->DisposeObject(app->fav_win_obj);
        app->fav_win_obj = NULL;
        app->fav_lb = NULL;
        IListBrowser->FreeListBrowserList(&app->fav_list);
        IExec->NewList(&app->fav_list);
        return;
    }
}

static void handle_favorites_events(TtxApp *app) {
    if (!app->fav_win_obj) return;
    ULONG result;
    WORD code;
    bool close_fav = false;

    while (!close_fav &&
           (result = IIntuition->IDoMethod(app->fav_win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                close_fav = true;
                break;
            case WMHI_GADGETUP: {
                ULONG gid = result & WMHI_GADGETMASK;
                LONG sel = -1;
                IIntuition->GetAttr(LISTBROWSER_Selected, app->fav_lb, (ULONG *)&sel);
                if (gid == 1) {
                    ULONG rel = 0;
                    IIntuition->GetAttr(LISTBROWSER_RelEvent, app->fav_lb, &rel);
                    if ((rel & LBRE_DOUBLECLICK) && sel >= 0 && sel < app->fav_count) {
                        gui_load_page(app, app->favs[sel].channel_idx, app->favs[sel].page, 1, true, false);
                    }
                } else if (gid == 2 && sel >= 0 && sel < app->fav_count) {
                    gui_load_page(app, app->favs[sel].channel_idx, app->favs[sel].page, 1, true, false);
                } else if (gid == 3) {
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "%s Seite %d",
                             g_ttx_channels[app->cur_page.channel_idx].name, app->cur_page.page);
                    if (favs_add(app->favs, &app->fav_count, TTX_MAX_FAVS,
                                 app->cur_page.channel_idx, app->cur_page.page, lbl)) {
                        favs_save(app->favs, app->fav_count);
                        IIntuition->SetGadgetAttrs((struct Gadget *)app->fav_lb, app->fav_win, NULL,
                                                   LISTBROWSER_Labels, ~0, TAG_DONE);
                        rebuild_fav_list(&app->fav_list, app->favs, app->fav_count);
                        IIntuition->SetGadgetAttrs((struct Gadget *)app->fav_lb, app->fav_win, NULL,
                                                   LISTBROWSER_Labels, (ULONG)&app->fav_list, TAG_DONE);
                    }
                } else if (gid == 4 && sel >= 0 && sel < app->fav_count) {
                    favs_remove(app->favs, &app->fav_count, sel);
                    favs_save(app->favs, app->fav_count);
                    IIntuition->SetGadgetAttrs((struct Gadget *)app->fav_lb, app->fav_win, NULL,
                                               LISTBROWSER_Labels, ~0, TAG_DONE);
                    rebuild_fav_list(&app->fav_list, app->favs, app->fav_count);
                    IIntuition->SetGadgetAttrs((struct Gadget *)app->fav_lb, app->fav_win, NULL,
                                               LISTBROWSER_Labels, (ULONG)&app->fav_list, TAG_DONE);
                } else if (gid == 5) {
                    close_fav = true;
                }
                break;
            }
        }
    }

    if (close_fav) {
        close_favorites_dialog(app);
    }
}

/* ============================================================================
 * Main Window Setup & Event Loop
 * ========================================================================== */

static Object *make_btn(ULONG gid, const char *label) {
    return IIntuition->NewObject(NULL, "button.gadget",
        GA_ID,        gid,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)label,
        TAG_DONE);
}

static struct Hook s_idcmp_hook;
static struct Hook s_space_render_hook;

static uint32 space_render_func(struct Hook *hook, Object *space_obj, struct gpRender *gpr) {
    (void)space_obj;
    TtxApp *app = (TtxApp *)hook->h_Data;
    if (!app || !gpr || !gpr->gpr_RPort) return 0;
    gui_redraw_canvas_rp(app, gpr->gpr_RPort);
    return 0;
}

static uint32 idcmp_hook_func(struct Hook *hook, Object *win_obj, struct IntuiMessage *imsg) {
    (void)win_obj;
    TtxApp *app = (TtxApp *)hook->h_Data;
    if (!app || !imsg) return 0;

    if (imsg->Class == IDCMP_EXTENDEDMOUSE && (imsg->Code & IMSGCODE_INTUIWHEELDATA)) {
        struct IntuiWheelData *wd = (struct IntuiWheelData *)imsg->IAddress;
        if (wd && wd->WheelY != 0) {
            app->wheel_delta += (wd->WheelY < 0) ? 1 : -1;
            app->wheel_mx = imsg->MouseX;
            app->wheel_my = imsg->MouseY;
        }
    }
    return 0;
}

static void gui_handle_digit(TtxApp *app, int digit) {
    if (digit < 0 || digit > 9) return;
    if (app->num_len == 0 && (digit == 0 || digit == 9)) return;

    app->num_buf[app->num_len++] = (char)('0' + digit);
    app->num_buf[app->num_len] = '\0';

    gui_update_keypad_display(app, app->num_buf);
    if (app->gadgets[GID_STR_PAGE]) {
        IIntuition->SetGadgetAttrs((struct Gadget *)app->gadgets[GID_STR_PAGE], app->win, NULL,
                                   STRINGA_TextVal, (ULONG)app->num_buf, TAG_DONE);
    }

    if (app->num_len == 3) {
        int pg = atoi(app->num_buf);
        app->num_len = 0;
        app->num_buf[0] = '\0';
        gui_load_page(app, app->cur_page.channel_idx, pg, 1, true, false);
    }
}

bool gui_init(TtxApp *app) {
    memset(app, 0, sizeof(*app));
    app->zoom_pct = 100;
    app->render_mutex = IExec->AllocSysObjectTags(ASOT_MUTEX, TAG_DONE);
    app->scaled_cap = 262144; /* 1 MB fixed strip buffer: safe inside SPACE_RenderHook */
    app->scaled_pixels = (uint32_t *)malloc((size_t)app->scaled_cap * sizeof(uint32_t));
    if (!app->scaled_pixels) {
        app->scaled_cap = 0;
    }
    ttx_cache_init();
    ttx_render_init(&app->renderer);
    prefs_load(&app->prefs);
    app->renderer.reveal = app->prefs.reveal_mode;
    favs_load(app->favs, &app->fav_count, TTX_MAX_FAVS);

    /* Sync Fit-Width menu checkmark with loaded prefs */
    for (int i = 0; s_menu[i].nm_Type != NM_END; i++) {
        if ((ULONG)s_menu[i].nm_UserData == MID_FUNC_FIT_WIDTH) {
            if (app->prefs.fit_width) s_menu[i].nm_Flags |= CHECKED;
            else                      s_menu[i].nm_Flags &= (UWORD)~CHECKED;
        }
    }

    s_idcmp_hook.h_Entry = (HOOKFUNC)idcmp_hook_func;
    s_idcmp_hook.h_Data  = (APTR)app;

    s_space_render_hook.h_Entry = (HOOKFUNC)space_render_func;
    s_space_render_hook.h_Data  = (APTR)app;

    IExec->NewList(&app->chan_list);
    for (int i = 0; i < TTX_MAX_CHANS; i++) {
        struct Node *n = IChooser->AllocChooserNode(CNA_Text, (ULONG)g_ttx_channels[i].name, TAG_DONE);
        if (n) IExec->AddTail(&app->chan_list, n);
    }

    app->win_obj = IIntuition->NewObject(NULL, "window.class",
        WA_Title,          (ULONG)"VortexTeletext",
        WA_Width,          510,
        WA_Height,         600,
        WA_MinWidth,       420,
        WA_MinHeight,      500,
        WA_DragBar,        TRUE,
        WA_DepthGadget,    TRUE,
        WA_SizeGadget,     TRUE,
        WA_CloseGadget,    TRUE,
        WA_Activate,       TRUE,
        WA_NewLookMenus,   TRUE,
        WA_ReportMouse,    TRUE,
        WA_IDCMP,          IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_GADGETUP |
                           IDCMP_RAWKEY | IDCMP_VANILLAKEY | IDCMP_MOUSEBUTTONS |
                           IDCMP_MOUSEMOVE | IDCMP_EXTENDEDMOUSE | IDCMP_INTUITICKS |
                           IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        WINDOW_IDCMPHook,     (ULONG)&s_idcmp_hook,
        WINDOW_IDCMPHookBits, IDCMP_EXTENDEDMOUSE,
        WINDOW_Position,   WPOS_CENTERSCREEN,
        WINDOW_NewMenu,    (ULONG)s_menu,
        WINDOW_Layout, IIntuition->NewObject(NULL, "layout.gadget",
            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
            LAYOUT_SpaceOuter,  TRUE,
            LAYOUT_SpaceInner,  TRUE,

            /* Teletext 40x25 Display */
            LAYOUT_AddChild, app->gadgets[GID_CANVAS] = IIntuition->NewObject(NULL, "space.gadget",
                GA_ID,             GID_CANVAS,
                GA_ReadOnly,       TRUE,
                SPACE_MinWidth,    400,
                SPACE_MinHeight,   312,
                SPACE_Transparent, TRUE,
                SPACE_BevelStyle,  BVS_NONE,
                SPACE_RenderHook,  (ULONG)&s_space_render_hook,
                TAG_DONE),
            CHILD_WeightedHeight, 100,

            /* Group 1: Seiten Nummer (4x4 Keypad like AmigaTText) */
            LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_BevelStyle,  BVS_GROUP,
                LAYOUT_Label,       (ULONG)"Seiten Nummer",
                LAYOUT_SpaceOuter,  TRUE,
                LAYOUT_SpaceInner,  TRUE,

                /* Row 1: 7, 8, 9, [Display] */
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize,    TRUE,
                    LAYOUT_SpaceInner,  TRUE,
                    LAYOUT_AddChild, app->gadgets[GID_KEY_7] = make_btn(GID_KEY_7, "7"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_8] = make_btn(GID_KEY_8, "8"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_9] = make_btn(GID_KEY_9, "9"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_DISPLAY] = IIntuition->NewObject(NULL, "button.gadget",
                        GA_ID,             GID_KEY_DISPLAY,
                        GA_ReadOnly,       TRUE,
                        BUTTON_BevelStyle, BVS_THIN,
                        GA_Text,           (ULONG)"",
                        TAG_DONE),
                    TAG_DONE),

                /* Row 2: 4, 5, 6, Gehe zu */
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize,    TRUE,
                    LAYOUT_SpaceInner,  TRUE,
                    LAYOUT_AddChild, app->gadgets[GID_KEY_4]  = make_btn(GID_KEY_4, "4"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_5]  = make_btn(GID_KEY_5, "5"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_6]  = make_btn(GID_KEY_6, "6"),
                    LAYOUT_AddChild, app->gadgets[GID_BTN_GO] = make_btn(GID_BTN_GO, "Gehe zu"),
                    TAG_DONE),

                /* Row 3: 1, 2, 3, Unterseite vor */
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize,    TRUE,
                    LAYOUT_SpaceInner,  TRUE,
                    LAYOUT_AddChild, app->gadgets[GID_KEY_1]        = make_btn(GID_KEY_1, "1"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_2]        = make_btn(GID_KEY_2, "2"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_3]        = make_btn(GID_KEY_3, "3"),
                    LAYOUT_AddChild, app->gadgets[GID_BTN_NEXT_SUB] = make_btn(GID_BTN_NEXT_SUB, "Unterseite vor"),
                    TAG_DONE),

                /* Row 4: -1, 0, +1, Unterseite zurueck */
                LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                    LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                    LAYOUT_EvenSize,    TRUE,
                    LAYOUT_SpaceInner,  TRUE,
                    LAYOUT_AddChild, app->gadgets[GID_BTN_PREV_PAGE] = make_btn(GID_BTN_PREV_PAGE, "-1"),
                    LAYOUT_AddChild, app->gadgets[GID_KEY_0]         = make_btn(GID_KEY_0, "0"),
                    LAYOUT_AddChild, app->gadgets[GID_BTN_NEXT_PAGE] = make_btn(GID_BTN_NEXT_PAGE, "+1"),
                    LAYOUT_AddChild, app->gadgets[GID_BTN_PREV_SUB]  = make_btn(GID_BTN_PREV_SUB, "Unterseite zurueck"),
                    TAG_DONE),

                TAG_DONE),
            CHILD_WeightedHeight, 0,

            /* Group 2: Seiten Nummer Auswahl */
            LAYOUT_AddChild, IIntuition->NewObject(NULL, "layout.gadget",
                LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
                LAYOUT_BevelStyle,  BVS_GROUP,
                LAYOUT_Label,       (ULONG)"Seiten Nummer Auswahl",
                LAYOUT_EvenSize,    TRUE,
                LAYOUT_SpaceOuter,  TRUE,
                LAYOUT_SpaceInner,  TRUE,

                LAYOUT_AddChild, app->gadgets[GID_STR_PAGE] = IIntuition->NewObject(NULL, "string.gadget",
                    GA_ID,           GID_STR_PAGE,
                    GA_RelVerify,    TRUE,
                    STRINGA_TextVal, (ULONG)"100",
                    STRINGA_MaxChars, 4,
                    TAG_DONE),

                LAYOUT_AddChild, app->gadgets[GID_BTN_RELOAD] = make_btn(GID_BTN_RELOAD, "Neu laden"),

                LAYOUT_AddChild, app->gadgets[GID_CHOOSER_CHAN] = IIntuition->NewObject(NULL, "chooser.gadget",
                    GA_ID,            GID_CHOOSER_CHAN,
                    GA_RelVerify,     TRUE,
                    CHOOSER_PopUp,    TRUE,
                    CHOOSER_Labels,   (ULONG)&app->chan_list,
                    CHOOSER_Selected, (ULONG)app->prefs.default_channel,
                    TAG_DONE),

                TAG_DONE),
            CHILD_WeightedHeight, 0,

            TAG_DONE),
        TAG_DONE);

    if (!app->win_obj) return false;
    app->win = (struct Window *)IIntuition->IDoMethod(app->win_obj, WM_OPEN, NULL);
    if (!app->win) return false;

    app->running = true;
    gui_load_page(app, app->prefs.default_channel, app->prefs.default_page, 1, false, false);
    return true;
}

void gui_cleanup(TtxApp *app) {
    prefs_save(&app->prefs);
    favs_save(app->favs, app->fav_count);

    close_favorites_dialog(app);

    if (app->win_obj) {
        IIntuition->IDoMethod(app->win_obj, WM_CLOSE, NULL);
        IIntuition->DisposeObject(app->win_obj);
        app->win_obj = NULL;
        app->win = NULL;
    }

    if (app->scaled_pixels) {
        free(app->scaled_pixels);
        app->scaled_pixels = NULL;
        app->scaled_cap = 0;
    }
    if (app->render_mutex) {
        IExec->FreeSysObject(ASOT_MUTEX, app->render_mutex);
        app->render_mutex = NULL;
    }

    struct Node *n;
    while ((n = IExec->RemHead(&app->chan_list)) != NULL) {
        IChooser->FreeChooserNode(n);
    }
}

static void handle_menu(TtxApp *app, ULONG mid) {
    switch (mid) {
        case MID_PROJ_HOME:
            gui_load_page(app, app->cur_page.channel_idx, 100, 1, true, false);
            break;
        case MID_PROJ_RELOAD:
            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.page, app->cur_page.subpage, false, true);
            break;
        case MID_PROJ_EXPORT_TXT:
            gui_export_current(app, false);
            break;
        case MID_PROJ_EXPORT_BMP:
            gui_export_current(app, true);
            break;
        case MID_PROJ_ABOUT:
            show_info_requester(app->win, "Ueber VortexTeletext",
                "VortexTeletext 1.0 (AmigaOS 4.1 PowerPC)\n\n"
                "Nativer Live-Teletext-Client fuer ARD, ZDF, 3SAT, HR, WDR & SWR.\n"
                "Tasten: 0-9 Seite waehlen, +/- Seite hoch/runter,\n"
                "Pfeile Navigation, R/F5 Neu laden, Backspace Zurueck,\n"
                "F Suche, B Favoriten, S BMP-Export.");
            break;
        case MID_PROJ_QUIT:
            app->running = false;
            break;
        case MID_NAV_BACK:
            gui_history_back(app);
            break;
        case MID_NAV_PREV_PAGE: {
            int np = (app->cur_page.page > 100) ? app->cur_page.page - 1 : 899;
            gui_load_page(app, app->cur_page.channel_idx, np, 1, true, false);
            break;
        }
        case MID_NAV_NEXT_PAGE: {
            int np = (app->cur_page.page < 899) ? app->cur_page.page + 1 : 100;
            gui_load_page(app, app->cur_page.channel_idx, np, 1, true, false);
            break;
        }
        case MID_NAV_PREV_SUB: {
            int ns = (app->cur_page.subpage > 1) ? app->cur_page.subpage - 1 : app->cur_page.total_subpages;
            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.page, ns, false, false);
            break;
        }
        case MID_NAV_NEXT_SUB: {
            int ns = (app->cur_page.subpage < app->cur_page.total_subpages) ? app->cur_page.subpage + 1 : 1;
            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.page, ns, false, false);
            break;
        }
        case MID_FUNC_SEARCH:
            open_search_dialog(app);
            break;
        case MID_FUNC_FAVS:
            open_favorites_dialog(app);
            break;
        case MID_FUNC_REVEAL:
            app->renderer.reveal = !app->renderer.reveal;
            app->prefs.reveal_mode = app->renderer.reveal;
            ttx_render_page(&app->renderer, &app->cur_page);
            gui_redraw_canvas(app);
            break;
        case MID_FUNC_FLASH:
            app->prefs.flash_enabled = !app->prefs.flash_enabled;
            if (!app->prefs.flash_enabled) app->renderer.flash_on = true;
            ttx_render_page(&app->renderer, &app->cur_page);
            gui_redraw_canvas(app);
            break;
        case MID_FUNC_FIT_WIDTH:
            app->prefs.fit_width = !app->prefs.fit_width;
            prefs_save(&app->prefs);
            gui_redraw_canvas(app);
            break;
        case MID_FUNC_ZOOM_RESET:
            app->zoom_pct = 100;
            app->view_x = 0;
            app->view_y = 0;
            gui_redraw_canvas(app);
            gui_update_status(app);
            break;
    }
}

void gui_run_loop(TtxApp *app) {
    ULONG sigmask = 0;
    IIntuition->GetAttr(WINDOW_SigMask, app->win_obj, &sigmask);

    while (app->running) {
        ULONG wait_mask = sigmask;
        if (app->fav_win_obj) {
            ULONG fav_mask = 0;
            IIntuition->GetAttr(WINDOW_SigMask, app->fav_win_obj, &fav_mask);
            wait_mask |= fav_mask;
        }
        IExec->Wait(wait_mask);
        if (app->fav_win_obj) {
            handle_favorites_events(app);
        }
        ULONG result;
        WORD code;
        while ((result = IIntuition->IDoMethod(app->win_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
            switch (result & WMHI_CLASSMASK) {
                case WMHI_CLOSEWINDOW:
                    app->running = false;
                    break;

                case WMHI_NEWSIZE:
                    gui_redraw_canvas(app);
                    break;

                case WMHI_MENUPICK: {
                    struct Menu *m = app->win->MenuStrip;
                    UWORD mnum = code;
                    while (mnum != MENUNULL && m) {
                        struct MenuItem *item = IIntuition->ItemAddress(m, mnum);
                        if (item) {
                            ULONG mid = (ULONG)GTMENUITEM_USERDATA(item);
                            handle_menu(app, mid);
                            mnum = item->NextSelect;
                        } else {
                            break;
                        }
                    }
                    break;
                }

                case WMHI_GADGETUP: {
                    ULONG gid = result & WMHI_GADGETMASK;
                    if (gid >= GID_KEY_0 && gid <= GID_KEY_9) {
                        gui_handle_digit(app, (int)(gid - GID_KEY_0));
                        break;
                    }
                    switch (gid) {
                        case GID_CHOOSER_CHAN: {
                            ULONG sel = 0;
                            IIntuition->GetAttr(CHOOSER_Selected, app->gadgets[GID_CHOOSER_CHAN], &sel);
                            app->num_len = 0;
                            app->num_buf[0] = '\0';
                            gui_load_page(app, (int)sel, 100, 1, true, false);
                            break;
                        }
                        case GID_STR_PAGE:
                        case GID_BTN_GO: {
                            int pg = 100;
                            if (app->num_len == 3) {
                                pg = atoi(app->num_buf);
                            } else {
                                STRPTR txt = NULL;
                                IIntuition->GetAttr(STRINGA_TextVal, app->gadgets[GID_STR_PAGE], (ULONG *)&txt);
                                pg = txt ? atoi((const char *)txt) : 100;
                            }
                            app->num_len = 0;
                            app->num_buf[0] = '\0';
                            gui_load_page(app, app->cur_page.channel_idx, pg, 1, true, false);
                            break;
                        }
                        case GID_BTN_PREV_PAGE:
                            handle_menu(app, MID_NAV_PREV_PAGE);
                            break;
                        case GID_BTN_NEXT_PAGE:
                            handle_menu(app, MID_NAV_NEXT_PAGE);
                            break;
                        case GID_BTN_PREV_SUB:
                            handle_menu(app, MID_NAV_PREV_SUB);
                            break;
                        case GID_BTN_NEXT_SUB:
                            handle_menu(app, MID_NAV_NEXT_SUB);
                            break;
                        case GID_BTN_RELOAD:
                            handle_menu(app, MID_PROJ_RELOAD);
                            break;
                    }
                    break;
                }

                case WMHI_MOUSEBUTTONS: {
                    if (app->win && code == SELECTDOWN) {
                        int row = 0, col = 0;
                        if (canvas_hit_test(app, app->win->MouseX, app->win->MouseY, &row, &col)) {
                            uint16_t lp = app->cur_page.cells[row][col].link_page;
                            if (lp >= 100 && lp <= 899) {
                                gui_load_page(app, app->cur_page.channel_idx, lp, 1, true, false);
                            }
                        }
                    } else if (app->win && code == MIDDLEDOWN) {
                        handle_menu(app, MID_FUNC_ZOOM_RESET);
                    }
                    break;
                }

                case WMHI_MOUSEMOVE: {
                    if (app->win) {
                        int row = 0, col = 0;
                        uint16_t new_hl = 0;
                        if (canvas_hit_test(app, app->win->MouseX, app->win->MouseY, &row, &col)) {
                            new_hl = app->cur_page.cells[row][col].link_page;
                        }
                        if (new_hl != app->renderer.hover_link) {
                            app->renderer.hover_link = new_hl;
                            ttx_render_page(&app->renderer, &app->cur_page);
                            gui_redraw_canvas(app);
                            gui_update_status(app);
                        }
                    }
                    break;
                }

                case WMHI_VANILLAKEY: {
                    char ch = (char)(code & 0xFF);
                    if (ch >= '0' && ch <= '9') {
                        gui_handle_digit(app, ch - '0');
                    } else if (ch == '+') {
                        handle_menu(app, MID_NAV_NEXT_PAGE);
                    } else if (ch == '-') {
                        handle_menu(app, MID_NAV_PREV_PAGE);
                    } else if (ch == '\r' || ch == '\n') {
                        if (app->num_len > 0) {
                            int pg = atoi(app->num_buf);
                            app->num_len = 0;
                            app->num_buf[0] = '\0';
                            if (pg >= 100 && pg <= 899) {
                                gui_load_page(app, app->cur_page.channel_idx, pg, 1, true, false);
                            }
                        }
                    } else if (ch == '\b') {
                        if (app->num_len > 0) {
                            app->num_len--;
                            app->num_buf[app->num_len] = '\0';
                            gui_update_keypad_display(app, app->num_buf);
                        } else {
                            gui_history_back(app);
                        }
                    } else if (ch == 'r' || ch == 'R') {
                        handle_menu(app, MID_PROJ_RELOAD);
                    }
                    break;
                }

                case WMHI_RAWKEY: {
                    switch (code) {
                        case 0x4F: /* Cursor Left */
                            handle_menu(app, MID_NAV_PREV_PAGE);
                            break;
                        case 0x4E: /* Cursor Right */
                            handle_menu(app, MID_NAV_NEXT_PAGE);
                            break;
                        case 0x4C: /* Cursor Up */
                            handle_menu(app, MID_NAV_NEXT_SUB);
                            break;
                        case 0x4D: /* Cursor Down */
                            handle_menu(app, MID_NAV_PREV_SUB);
                            break;
                        case 0x50: /* F1 = Red */
                            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.red_page, 1, true, false);
                            break;
                        case 0x51: /* F2 = Green */
                            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.green_page, 1, true, false);
                            break;
                        case 0x52: /* F3 = Yellow */
                            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.yellow_page, 1, true, false);
                            break;
                        case 0x53: /* F4 = Cyan */
                            gui_load_page(app, app->cur_page.channel_idx, app->cur_page.cyan_page, 1, true, false);
                            break;
                        case 0x54: /* F5 = Reload */
                            handle_menu(app, MID_PROJ_RELOAD);
                            break;
                        case 0x7A: /* RAWKEY_NM_WHEEL_UP fallback */
                            if (app->win) {
                                apply_mouse_wheel_zoom(app, 1, app->win->MouseX, app->win->MouseY);
                            }
                            break;
                        case 0x7B: /* RAWKEY_NM_WHEEL_DOWN fallback */
                            if (app->win) {
                                apply_mouse_wheel_zoom(app, -1, app->win->MouseX, app->win->MouseY);
                            }
                            break;
                    }
                    break;
                }

                case WMHI_INTUITICK: {
                    if (app->prefs.flash_enabled) {
                        app->tick_count++;
                        if (app->tick_count >= 5) {
                            app->tick_count = 0;
                            app->renderer.flash_on = !app->renderer.flash_on;
                            ttx_render_page(&app->renderer, &app->cur_page);
                            gui_redraw_canvas(app);
                        }
                    }
                    break;
                }
            }
        }
        if (app->wheel_delta != 0) {
            int steps = app->wheel_delta;
            int mx = app->wheel_mx;
            int my = app->wheel_my;
            app->wheel_delta = 0;
            apply_mouse_wheel_zoom(app, steps, mx, my);
        }
    }
}
