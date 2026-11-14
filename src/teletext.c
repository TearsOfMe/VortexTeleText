#include "teletext.h"
#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const TtxChannel g_ttx_channels[TTX_MAX_CHANS] = {
    { "ard",  "ARD",  "ard",  TTX_SRC_ARD },
    { "zdf",  "ZDF",  "zdf",  TTX_SRC_ZDF },
    { "3sat", "3SAT", "3sat", TTX_SRC_ZDF },
    { "hr",   "HR",   "hr",   TTX_SRC_HR  },
    { "wdr",  "WDR",  "wdr",  TTX_SRC_WDR },
    { "swr",  "SWR",  "swr",  TTX_SRC_SWR }
};

/* ============================================================================
 * Page initialization & automatic 3-digit page link detection
 * ========================================================================== */

void ttx_page_init(TtxPage *pg, int channel_idx, int page, int subpage) {
    memset(pg, 0, sizeof(*pg));
    pg->channel_idx = channel_idx;
    pg->page = page;
    pg->subpage = subpage > 0 ? subpage : 1;
    pg->total_subpages = 1;
    pg->prev_page = (page > 100) ? page - 1 : 899;
    pg->next_page = (page < 899) ? page + 1 : 100;
    pg->red_page = pg->prev_page;
    pg->green_page = pg->next_page;
    pg->yellow_page = 100;
    pg->cyan_page = 101;
    pg->valid = false;

    for (int r = 0; r < TTX_ROWS; r++) {
        for (int c = 0; c < TTX_COLS; c++) {
            pg->cells[r][c].ch = ' ';
            pg->cells[r][c].fg = TTX_WHITE;
            pg->cells[r][c].bg = TTX_BLACK;
            pg->cells[r][c].attrs = 0;
            pg->cells[r][c].link_page = 0;
        }
    }
}

void ttx_page_detect_links(TtxPage *pg) {
    for (int r = 1; r < TTX_ROWS; r++) {
        for (int c = 0; c <= TTX_COLS - 3; c++) {
            /* Skip mosaic cells */
            if (pg->cells[r][c].attrs & TTX_ATTR_MOSAIC) continue;
            uint8_t c0 = pg->cells[r][c].ch;
            uint8_t c1 = pg->cells[r][c + 1].ch;
            uint8_t c2 = pg->cells[r][c + 2].ch;
            if (c0 >= '1' && c0 <= '8' && isdigit(c1) && isdigit(c2)) {
                bool left_ok  = (c == 0) || !isdigit(pg->cells[r][c - 1].ch);
                bool right_ok = (c + 3 >= TTX_COLS) || !isdigit(pg->cells[r][c + 3].ch);
                if (left_ok && right_ok) {
                    uint16_t target = (uint16_t)((c0 - '0') * 100 + (c1 - '0') * 10 + (c2 - '0'));
                    for (int k = 0; k < 3; k++) {
                        if (pg->cells[r][c + k].link_page == 0) {
                            pg->cells[r][c + k].link_page = target;
                            pg->cells[r][c + k].attrs |= TTX_ATTR_LINK;
                        }
                    }
                    c += 2;
                }
            }
        }
    }
}

/* ============================================================================
 * Helpers: Base64, JSON minimal field extractor, UTF-8/HTML entity -> Latin-1
 * ========================================================================== */

static int b64_val(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(const char *src, int src_len, uint8_t *dst, int dst_max) {
    int out = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (int i = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '=') break;
        int v = b64_val(c);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (out < dst_max) {
                dst[out++] = (uint8_t)((acc >> bits) & 0xFF);
            }
        }
    }
    return out;
}

static uint8_t utf8_to_latin1_char(const char *s, int *consumed) {
    unsigned char b0 = (unsigned char)s[0];
    if (b0 < 0x80) {
        *consumed = 1;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && s[1]) {
        unsigned char b1 = (unsigned char)s[1];
        *consumed = 2;
        uint16_t cp = (uint16_t)(((b0 & 0x1F) << 6) | (b1 & 0x3F));
        return (cp <= 0xFF) ? (uint8_t)cp : '?';
    }
    if ((b0 & 0xF0) == 0xE0 && s[1] && s[2]) {
        *consumed = 3;
        return ' ';
    }
    if ((b0 & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *consumed = 4;
        return ' ';
    }
    *consumed = 1;
    return '?';
}

/* Extract JSON string value for a given key starting from `start` */
static bool json_get_string(const char *json, const char *key, char *out, int out_sz, const char **next_pos) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return false;
    p++;

    int o = 0;
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            char esc = *p++;
            char ch = esc;
            if (esc == 'n') ch = '\n';
            else if (esc == 'r') ch = '\r';
            else if (esc == 't') ch = '\t';
            else if (esc == 'u' && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) &&
                     isxdigit((unsigned char)p[2]) && isxdigit((unsigned char)p[3])) {
                char hex[5] = { p[0], p[1], p[2], p[3], '\0' };
                unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                p += 4;
                ch = (cp <= 0xFF) ? (char)cp : ' ';
            }
            if (o < out_sz - 1) out[o++] = ch;
        } else {
            int cons = 1;
            uint8_t l1 = utf8_to_latin1_char(p, &cons);
            p += cons;
            if (o < out_sz - 1) out[o++] = (char)l1;
        }
    }
    out[o] = '\0';
    if (*p == '"') p++;
    if (next_pos) *next_pos = p;
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out_val) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '-' && !isdigit((unsigned char)*p)) return false;
    *out_val = atoi(p);
    return true;
}

/* ============================================================================
 * 1. SRF / RTS / RSI Parser (EBU ETS 300 706 Raw Level 1.5 ep1Format.content)
 * ========================================================================== */

static uint8_t ttx_map_national_char(uint8_t b, int lang_code) {
    /* ETS 300 706 National Option Character Subsets:
     * 13 = German (used by SRF), 15 = French (RTS), 16 = Italian (RSI) */
    if (lang_code == 13 || lang_code == 4) {
        switch (b) {
            case 0x23: return '#';
            case 0x24: return '$';
            case 0x40: return 0xA7; /* Section sign § */
            case 0x5B: return 0xC4; /* Ä */
            case 0x5C: return 0xD6; /* Ö */
            case 0x5D: return 0xDC; /* Ü */
            case 0x5E: return '^';
            case 0x5F: return '_';
            case 0x60: return 0xB0; /* ° */
            case 0x7B: return 0xE4; /* ä */
            case 0x7C: return 0xF6; /* ö */
            case 0x7D: return 0xFC; /* ü */
            case 0x7E: return 0xDF; /* ß */
            default: break;
        }
    } else if (lang_code == 15 || lang_code == 1) {
        switch (b) {
            case 0x23: return 0xE9; /* é */
            case 0x24: return 0xEF; /* ï */
            case 0x40: return 0xE0; /* à */
            case 0x5B: return 0xEB; /* ë */
            case 0x5C: return 0xEA; /* ê */
            case 0x5D: return 0xF9; /* ù */
            case 0x5E: return 0xEE; /* î */
            case 0x5F: return '#';
            case 0x60: return 0xE8; /* è */
            case 0x7B: return 0xE2; /* â */
            case 0x7C: return 0xF4; /* ô */
            case 0x7D: return 0xFB; /* û */
            case 0x7E: return 0xE7; /* ç */
            default: break;
        }
    } else if (lang_code == 16 || lang_code == 2) {
        switch (b) {
            case 0x23: return 0xA3; /* £ */
            case 0x40: return 0xE9; /* é */
            case 0x5B: return 0xB0; /* ° */
            case 0x5C: return 0xE7; /* ç */
            case 0x5D: return 0xBB; /* » */
            case 0x60: return 0xF9; /* ù */
            case 0x7B: return 0xE0; /* à */
            case 0x7C: return 0xF2; /* ò */
            case 0x7D: return 0xE8; /* è */
            case 0x7E: return 0xEC; /* ì */
            default: break;
        }
    }
    if (b == 0x7F) return 0xA0;
    return b;
}

static void ttx_decode_ep1_bytes(const uint8_t *raw, int raw_len, int lang_code, TtxPage *pg) {
    int rows = raw_len / TTX_COLS;
    if (rows > TTX_ROWS) rows = TTX_ROWS;

    bool prev_row_double = false;

    for (int r = 0; r < rows; r++) {
        uint8_t fg = TTX_WHITE;
        uint8_t bg = TTX_BLACK;
        bool mosaic = false;
        bool separated = false;
        bool flash = false;
        bool double_h = false;
        bool conceal = false;
        bool hold_mosaic = false;
        uint8_t held_mosaic_ch = 0;

        /* Skip bottom half of a double-height row */
        if (prev_row_double) {
            prev_row_double = false;
            for (int c = 0; c < TTX_COLS; c++) {
                pg->cells[r][c].ch = ' ';
                pg->cells[r][c].fg = fg;
                pg->cells[r][c].bg = pg->cells[r - 1][c].bg;
                pg->cells[r][c].attrs = 0;
            }
            continue;
        }

        for (int c = 0; c < TTX_COLS; c++) {
            uint8_t b = raw[r * TTX_COLS + c] & 0x7F;

            if (b < 0x20) {
                /* Control code (Set-After vs Set-At) */
                bool set_at = false;
                switch (b) {
                    case 0x00: case 0x01: case 0x02: case 0x03:
                    case 0x04: case 0x05: case 0x06: case 0x07:
                        fg = b & 0x07;
                        mosaic = false;
                        conceal = false;
                        held_mosaic_ch = 0;
                        break;
                    case 0x08: flash = true; break;
                    case 0x09: flash = false; set_at = true; break;
                    case 0x0C: double_h = false; set_at = true; break;
                    case 0x0D: double_h = true; prev_row_double = true; break;
                    case 0x10: case 0x11: case 0x12: case 0x13:
                    case 0x14: case 0x15: case 0x16: case 0x17:
                        fg = b & 0x07;
                        mosaic = true;
                        conceal = false;
                        break;
                    case 0x18: conceal = true; set_at = true; break;
                    case 0x19: separated = false; set_at = true; break;
                    case 0x1A: separated = true; set_at = true; break;
                    case 0x1C: bg = TTX_BLACK; set_at = true; break;
                    case 0x1D: bg = fg; set_at = true; break;
                    case 0x1E: hold_mosaic = true; set_at = true; break;
                    case 0x1F: hold_mosaic = false; break;
                    default: break;
                }

                pg->cells[r][c].fg = fg;
                pg->cells[r][c].bg = bg;
                if (hold_mosaic && mosaic && held_mosaic_ch != 0) {
                    pg->cells[r][c].ch = held_mosaic_ch;
                    pg->cells[r][c].attrs = TTX_ATTR_MOSAIC | (separated ? TTX_ATTR_SEPARATED : 0);
                } else {
                    pg->cells[r][c].ch = ' ';
                    pg->cells[r][c].attrs = 0;
                }
                (void)set_at;
            } else {
                pg->cells[r][c].fg = fg;
                pg->cells[r][c].bg = bg;
                pg->cells[r][c].attrs = (flash ? TTX_ATTR_FLASH : 0) |
                                        (double_h ? TTX_ATTR_DOUBLE_H : 0) |
                                        (conceal ? TTX_ATTR_CONCEAL : 0);

                if (mosaic && ((b >= 0x20 && b <= 0x3F) || (b >= 0x60 && b <= 0x7F))) {
                    /* Map ETS 300 706 mosaic byte to 6-bit pattern (bit0..bit5) */
                    uint8_t m = (uint8_t)((b & 0x1F) | ((b & 0x40) ? 0x20 : 0x00));
                    pg->cells[r][c].ch = m;
                    pg->cells[r][c].attrs |= TTX_ATTR_MOSAIC | (separated ? TTX_ATTR_SEPARATED : 0);
                    held_mosaic_ch = m;
                } else {
                    pg->cells[r][c].ch = ttx_map_national_char(b, lang_code);
                }
            }
        }
    }
}

bool ttx_parse_srf_json(const char *json, int json_len, int channel_idx, int req_page, int req_sub, TtxPage *out) {
    (void)json_len;
    ttx_page_init(out, channel_idx, req_page, req_sub);

    int val = 0;
    if (json_get_int(json, "pageNumber", &val)) out->page = val;
    if (json_get_int(json, "nextPage", &val))   out->next_page = val;
    if (json_get_int(json, "prevPage", &val))   out->prev_page = val;

    /* Count subpages & locate requested subpage */
    const char *sub_arr = strstr(json, "\"subpages\"");
    if (!sub_arr) return false;

    int count = 0;
    const char *scan = sub_arr;
    const char *target_block = NULL;
    while ((scan = strstr(scan, "\"ep1Info\"")) != NULL) {
        count++;
        if (count == out->subpage || (out->subpage == 1 && count == 1)) {
            target_block = scan;
        }
        scan += 9;
    }
    if (count == 0) return false;
    out->total_subpages = count;
    if (!target_block) {
        out->subpage = 1;
        target_block = strstr(sub_arr, "\"ep1Info\"");
    }

    int lang_code = 13;
    json_get_int(target_block, "languageCode", &lang_code);

    char *b64 = (char *)malloc(4096);
    if (!b64) return false;
    if (!json_get_string(target_block, "content", b64, 4096, NULL)) {
        free(b64);
        return false;
    }

    uint8_t raw[TTX_ROWS * TTX_COLS + 64];
    int raw_len = base64_decode(b64, (int)strlen(b64), raw, sizeof(raw));
    free(b64);

    if (raw_len < 40) return false;
    ttx_decode_ep1_bytes(raw, raw_len, lang_code, out);

    /* Ensure header row 0 shows channel + page cleanly */
    char hdr[41];
    snprintf(hdr, sizeof(hdr), "P%-3d %-16s %03d/%02d",
             out->page, g_ttx_channels[channel_idx].name, out->page, out->subpage);
    for (int c = 0; c < TTX_COLS && hdr[c]; c++) {
        if (out->cells[0][c].ch == ' ' && c < 12) {
            out->cells[0][c].ch = (uint8_t)hdr[c];
        }
    }

    snprintf(out->title, sizeof(out->title), "%s - Seite %d", g_ttx_channels[channel_idx].name, out->page);
    ttx_page_detect_links(out);
    out->valid = true;
    return true;
}

/* ============================================================================
 * 2. ARD Text HTML Parser (https://www.ard-text.de/page_only.php?page=...)
 * ========================================================================== */

static uint8_t ard_color_code(const char *s, int len) {
    if (len == 1) {
        if (s[0] == 'b') return TTX_BLACK;
        if (s[0] == 'r') return TTX_RED;
        if (s[0] == 'g') return TTX_GREEN;
        if (s[0] == 'y') return TTX_YELLOW;
        if (s[0] == 'm') return TTX_MAGENTA;
        if (s[0] == 'c') return TTX_CYAN;
        if (s[0] == 'w') return TTX_WHITE;
    } else if (len == 2 && s[0] == 'b' && s[1] == 'l') {
        return TTX_BLUE;
    }
    return TTX_WHITE;
}

static uint8_t decode_html_entity(const char *ent, int *consumed) {
    const char *semi = strchr(ent, ';');
    if (!semi || (semi - ent) > 10) {
        *consumed = 1;
        return '&';
    }
    int elen = (int)(semi - ent);
    *consumed = elen + 1;

    if (strncmp(ent, "&nbsp;", 6) == 0)  return ' ';
    if (strncmp(ent, "&amp;", 5) == 0)   return '&';
    if (strncmp(ent, "&lt;", 4) == 0)    return '<';
    if (strncmp(ent, "&gt;", 4) == 0)    return '>';
    if (strncmp(ent, "&quot;", 6) == 0)  return '"';
    if (strncmp(ent, "&apos;", 6) == 0)  return '\'';
    if (strncmp(ent, "&auml;", 6) == 0)  return 0xE4;
    if (strncmp(ent, "&ouml;", 6) == 0)  return 0xF6;
    if (strncmp(ent, "&uuml;", 6) == 0)  return 0xFC;
    if (strncmp(ent, "&Auml;", 6) == 0)  return 0xC4;
    if (strncmp(ent, "&Ouml;", 6) == 0)  return 0xD6;
    if (strncmp(ent, "&Uuml;", 6) == 0)  return 0xDC;
    if (strncmp(ent, "&szlig;", 7) == 0) return 0xDF;
    if (strncmp(ent, "&deg;", 5) == 0)   return 0xB0;
    if (strncmp(ent, "&sect;", 6) == 0)  return 0xA7;
    if (strncmp(ent, "&euro;", 6) == 0)  return 'E';
    if (strncmp(ent, "&ndash;", 7) == 0) return '-';
    if (strncmp(ent, "&mdash;", 7) == 0) return '-';
    if (strncmp(ent, "&laquo;", 7) == 0) return 0xAB;
    if (strncmp(ent, "&raquo;", 7) == 0) return 0xBB;
    if (ent[1] == '#') {
        int cp = (ent[2] == 'x' || ent[2] == 'X')
                 ? (int)strtol(ent + 3, NULL, 16)
                 : atoi(ent + 2);
        return (cp >= 32 && cp <= 255) ? (uint8_t)cp : ' ';
    }
    return '?';
}

bool ttx_parse_ard_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out) {
    (void)html_len;
    ttx_page_init(out, channel_idx, req_page, req_sub);

    const char *p = strstr(html, "id='page_1'");
    if (!p) p = strstr(html, "id=\"page_1\"");
    if (!p) return false;

    /* Check subpage info if present e.g. "1/3" or hidden subpage counter */
    const char *sub_el = strstr(html, "subpageCounter");
    if (sub_el) {
        const char *gt = strchr(sub_el, '>');
        if (gt) {
            int cur_s = 1, tot_s = 1;
            if (sscanf(gt + 1, "%d/%d", &cur_s, &tot_s) == 2 && tot_s >= 1) {
                out->subpage = cur_s;
                out->total_subpages = tot_s;
            }
        }
    }

    int row = 0;
    p = strstr(p, "<div>");
    while (p && row < TTX_ROWS) {
        p += 5; /* skip <div> */
        const char *div_end = strstr(p, "</div>");
        if (!div_end) break;

        int col = 0;
        uint8_t fg = TTX_WHITE;
        uint8_t bg = TTX_BLACK;
        uint16_t cur_link = 0;

        const char *q = p;
        while (q < div_end && col < TTX_COLS) {
            if (*q == '<') {
                if (strncmp(q, "<span", 5) == 0) {
                    const char *cls = strstr(q, "class=");
                    const char *gt = strchr(q, '>');
                    if (cls && gt && cls < gt) {
                        const char *fg_p = strstr(cls, "fg");
                        if (fg_p && fg_p < gt) {
                            const char *e = fg_p + 2;
                            while (e < gt && isalpha((unsigned char)*e)) e++;
                            fg = ard_color_code(fg_p + 2, (int)(e - (fg_p + 2)));
                        }
                        const char *bg_p = strstr(cls, "bg");
                        if (bg_p && bg_p < gt) {
                            const char *e = bg_p + 2;
                            while (e < gt && isalpha((unsigned char)*e)) e++;
                            bg = ard_color_code(bg_p + 2, (int)(e - (bg_p + 2)));
                        }
                    }
                    q = gt ? gt + 1 : q + 1;
                } else if (strncmp(q, "<a ", 3) == 0) {
                    const char *pg_p = strstr(q, "page=");
                    const char *gt = strchr(q, '>');
                    if (pg_p && gt && pg_p < gt) {
                        cur_link = (uint16_t)atoi(pg_p + 5);
                    }
                    q = gt ? gt + 1 : q + 1;
                } else if (strncmp(q, "</a>", 4) == 0) {
                    cur_link = 0;
                    q += 4;
                } else if (strncmp(q, "<img", 4) == 0) {
                    /* Format: ./img/g1w70.gif -> g<sep><color><hex>.gif */
                    const char *gt = strchr(q, '>');
                    const char *img_p = strstr(q, "/img/g");
                    if (img_p && gt && img_p < gt) {
                        img_p += 6; /* skip /img/g */
                        bool sep = (*img_p == '0' || *img_p == '2'); /* g1 = contiguous */
                        if (*img_p) img_p++;
                        const char *col_start = img_p;
                        int clen = 0;
                        if (img_p[0] == 'b' && img_p[1] == 'l') { clen = 2; img_p += 2; }
                        else if (isalpha((unsigned char)img_p[0])) { clen = 1; img_p += 1; }
                        uint8_t mfg = ard_color_code(col_start, clen);
                        unsigned int mhex = 0x20;
                        sscanf(img_p, "%2x", &mhex);
                        uint8_t m6 = (uint8_t)((mhex & 0x1F) | ((mhex & 0x40) ? 0x20 : 0x00));
                        out->cells[row][col].ch = m6;
                        out->cells[row][col].fg = mfg;
                        out->cells[row][col].bg = bg;
                        out->cells[row][col].attrs = TTX_ATTR_MOSAIC | (sep ? TTX_ATTR_SEPARATED : 0);
                        out->cells[row][col].link_page = cur_link;
                        col++;
                    }
                    q = gt ? gt + 1 : q + 1;
                } else {
                    const char *gt = strchr(q, '>');
                    q = gt ? gt + 1 : q + 1;
                }
            } else if (*q == '&') {
                int cons = 1;
                uint8_t ch = decode_html_entity(q, &cons);
                q += cons;
                out->cells[row][col].ch = ch;
                out->cells[row][col].fg = fg;
                out->cells[row][col].bg = bg;
                out->cells[row][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                out->cells[row][col].link_page = cur_link;
                col++;
            } else if (*q == '\r' || *q == '\n') {
                q++;
            } else {
                int cons = 1;
                uint8_t ch = utf8_to_latin1_char(q, &cons);
                q += cons;
                out->cells[row][col].ch = ch;
                out->cells[row][col].fg = fg;
                out->cells[row][col].bg = bg;
                out->cells[row][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                out->cells[row][col].link_page = cur_link;
                col++;
            }
        }
        row++;
        p = strstr(div_end + 6, "<div>");
    }

    if (row == 0) return false;
    snprintf(out->title, sizeof(out->title), "ARD Text - Seite %d", out->page);
    ttx_page_detect_links(out);
    out->valid = true;
    return true;
}

/* ============================================================================
 * 3. HR Text Parser (https://www.hr-text.hr-fernsehen.de/ttxweb/?page=...)
 * ========================================================================== */

static int parse_pre_int(const char *html, const char *id) {
    char pat[64];
    snprintf(pat, sizeof(pat), "id=\"%s\">", id);
    const char *p = strstr(html, pat);
    if (!p) return 0;
    return atoi(p + strlen(pat));
}

bool ttx_parse_hr_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out) {
    (void)html_len;
    ttx_page_init(out, channel_idx, req_page, req_sub);

    int pv = parse_pre_int(html, "ttxPrevPageNum");
    int nv = parse_pre_int(html, "ttxNextPageNum");
    int ns = parse_pre_int(html, "ttxNumSubpages");
    if (pv >= 100 && pv <= 899) out->prev_page = pv;
    if (nv >= 100 && nv <= 899) out->next_page = nv;
    if (ns >= 1) out->total_subpages = ns;
    out->red_page = out->prev_page;
    out->green_page = out->next_page;

    const char *ttx_page = strstr(html, "id=\"ttxPage\"");
    if (!ttx_page) return false;

    int parsed_rows = 0;
    for (int r = 0; r < TTX_ROWS; r++) {
        char rpat[32];
        snprintf(rpat, sizeof(rpat), "id=\"row%d\">", r);
        const char *rp = strstr(ttx_page, rpat);
        if (!rp) continue;
        rp += strlen(rpat);
        const char *rend = strstr(rp, "</pre>");
        if (!rend) continue;

        parsed_rows++;
        int col = 0;
        uint8_t fg_stack[16] = { TTX_WHITE };
        uint8_t bg_stack[16] = { TTX_BLACK };
        int sp = 0;
        uint16_t cur_link = 0;

        const char *q = rp;
        while (q < rend && col < TTX_COLS) {
            if (*q == '<') {
                if (strncmp(q, "<span", 5) == 0) {
                    const char *gt = strchr(q, '>');
                    uint8_t nfg = fg_stack[sp];
                    uint8_t nbg = bg_stack[sp];
                    bool is_mosaic = false;
                    bool is_sep = false;
                    uint8_t m6 = 0;
                    uint8_t mfg = nfg;

                    const char *cls = strstr(q, "class=\"");
                    if (cls && gt && cls < gt) {
                        const char *cp = cls + 7;
                        const char *cend = strchr(cp, '"');
                        if (cend && cend < gt) {
                            const char *f = strstr(cp, "fg");
                            if (f && f + 2 < cend && isdigit((unsigned char)f[2])) nfg = (uint8_t)(f[2] - '0') & 7;
                            const char *b = strstr(cp, "bg");
                            if (b && b + 2 < cend && isdigit((unsigned char)b[2])) nbg = (uint8_t)(b[2] - '0') & 7;

                            /* Mosaic span: class="g1c47c" or "g0c47c" */
                            if ((cp[0] == 'g') && (cp[1] == '0' || cp[1] == '1') && cp[2] == 'c' &&
                                isdigit((unsigned char)cp[3]) && isxdigit((unsigned char)cp[4]) && isxdigit((unsigned char)cp[5])) {
                                is_mosaic = true;
                                is_sep = (cp[1] == '0');
                                mfg = (uint8_t)(cp[3] - '0') & 7;
                                unsigned int mhex = 0x20;
                                sscanf(cp + 4, "%2x", &mhex);
                                m6 = (uint8_t)((mhex & 0x1F) | ((mhex & 0x40) ? 0x20 : 0x00));
                            }
                        }
                    }

                    if (sp < 15) sp++;
                    fg_stack[sp] = nfg;
                    bg_stack[sp] = nbg;

                    if (is_mosaic && gt) {
                        const char *span_end = strstr(gt + 1, "</span>");
                        out->cells[r][col].ch = m6;
                        out->cells[r][col].fg = mfg;
                        out->cells[r][col].bg = nbg;
                        out->cells[r][col].attrs = TTX_ATTR_MOSAIC | (is_sep ? TTX_ATTR_SEPARATED : 0);
                        out->cells[r][col].link_page = cur_link;
                        col++;
                        if (sp > 0) sp--;
                        q = span_end ? span_end + 7 : gt + 1;
                        continue;
                    }
                    q = gt ? gt + 1 : q + 1;
                } else if (strncmp(q, "</span>", 7) == 0) {
                    if (sp > 0) sp--;
                    q += 7;
                } else if (strncmp(q, "<a ", 3) == 0) {
                    const char *pg_p = strstr(q, "page=");
                    const char *gt = strchr(q, '>');
                    if (pg_p && gt && pg_p < gt) cur_link = (uint16_t)atoi(pg_p + 5);
                    q = gt ? gt + 1 : q + 1;
                } else if (strncmp(q, "</a>", 4) == 0) {
                    cur_link = 0;
                    q += 4;
                } else {
                    const char *gt = strchr(q, '>');
                    q = gt ? gt + 1 : q + 1;
                }
            } else if (*q == '&') {
                int cons = 1;
                uint8_t ch = decode_html_entity(q, &cons);
                q += cons;
                out->cells[r][col].ch = ch;
                out->cells[r][col].fg = fg_stack[sp];
                out->cells[r][col].bg = bg_stack[sp];
                out->cells[r][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                out->cells[r][col].link_page = cur_link;
                col++;
            } else if (*q == '\r' || *q == '\n') {
                q++;
            } else {
                int cons = 1;
                uint8_t ch = utf8_to_latin1_char(q, &cons);
                q += cons;
                out->cells[r][col].ch = ch;
                out->cells[r][col].fg = fg_stack[sp];
                out->cells[r][col].bg = bg_stack[sp];
                out->cells[r][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                out->cells[r][col].link_page = cur_link;
                col++;
            }
        }
    }

    if (parsed_rows == 0) return false;
    snprintf(out->title, sizeof(out->title), "%s - Seite %d", g_ttx_channels[channel_idx].name, out->page);
    ttx_page_detect_links(out);
    out->valid = true;
    return true;
}

/* ============================================================================
 * 4. WDR Text Parser (https://www1.wdr.de/wdrtext/externvtx100~_eap__page__num-...)
 * ========================================================================== */

static uint8_t wdr_parse_color(const char *cls, const char *cend, bool bg) {
    if (bg) {
        if (strstr(cls, "bg_black")   && strstr(cls, "bg_black")   < cend) return TTX_BLACK;
        if (strstr(cls, "bg_red")     && strstr(cls, "bg_red")     < cend) return TTX_RED;
        if (strstr(cls, "bg_green")   && strstr(cls, "bg_green")   < cend) return TTX_GREEN;
        if (strstr(cls, "bg_yellow")  && strstr(cls, "bg_yellow")  < cend) return TTX_YELLOW;
        if (strstr(cls, "bg_blue")    && strstr(cls, "bg_blue")    < cend) return TTX_BLUE;
        if (strstr(cls, "bg_magenta") && strstr(cls, "bg_magenta") < cend) return TTX_MAGENTA;
        if (strstr(cls, "bg_cyan")    && strstr(cls, "bg_cyan")    < cend) return TTX_CYAN;
        if (strstr(cls, "bg_white")   && strstr(cls, "bg_white")   < cend) return TTX_WHITE;
        return TTX_BLACK;
    } else {
        /* Foreground token appears after bg_xxx */
        const char *p = cls;
        while (p < cend) {
            while (p < cend && *p == ' ') p++;
            const char *w = p;
            while (p < cend && *p != ' ') p++;
            int wlen = (int)(p - w);
            if (wlen == 5 && strncmp(w, "black", 5) == 0)   return TTX_BLACK;
            if (wlen == 3 && strncmp(w, "red", 3) == 0)     return TTX_RED;
            if (wlen == 5 && strncmp(w, "green", 5) == 0)   return TTX_GREEN;
            if (wlen == 6 && strncmp(w, "yellow", 6) == 0)  return TTX_YELLOW;
            if (wlen == 4 && strncmp(w, "blue", 4) == 0)    return TTX_BLUE;
            if (wlen == 7 && strncmp(w, "magenta", 7) == 0) return TTX_MAGENTA;
            if (wlen == 4 && strncmp(w, "cyan", 4) == 0)    return TTX_CYAN;
            if (wlen == 5 && strncmp(w, "white", 5) == 0)   return TTX_WHITE;
        }
        return TTX_WHITE;
    }
}

bool ttx_parse_wdr_html(const char *html, int html_len, int channel_idx, int req_page, int req_sub, TtxPage *out) {
    (void)html_len;
    ttx_page_init(out, channel_idx, req_page, req_sub);

    const char *cont = strstr(html, "vt_container");
    if (!cont) return false;

    /* Row 0 header */
    char hdr[41];
    snprintf(hdr, sizeof(hdr), "P%-3d   WDR Text         %03d/%02d  TELETEXT", out->page, out->page, out->subpage);
    for (int c = 0; c < TTX_COLS && hdr[c]; c++) {
        out->cells[0][c].ch = (uint8_t)hdr[c];
        out->cells[0][c].fg = (c < 5) ? TTX_YELLOW : TTX_WHITE;
        out->cells[0][c].bg = TTX_BLACK;
    }

    int parsed_rows = 0;
    for (int r = 1; r < TTX_ROWS; r++) {
        char rpat[32];
        snprintf(rpat, sizeof(rpat), "vt_row_%d\"", r);
        const char *rp = strstr(cont, rpat);
        if (!rp) continue;
        const char *rgt = strchr(rp, '>');
        if (!rgt) continue;
        rp = rgt + 1;

        char next_rpat[32];
        snprintf(next_rpat, sizeof(next_rpat), "vt_row_%d\"", r + 1);
        const char *rend = strstr(rp, next_rpat);
        if (!rend) rend = strstr(rp, "<!-- /vt_table");
        if (!rend) rend = rp + strlen(rp);

        parsed_rows++;
        int col = 0;
        const char *cp = strstr(rp, "vt_col");
        while (cp && cp < rend && col < TTX_COLS) {
            /* Walk back to class=" */
            const char *cls = cp;
            while (cls > rp && strncmp(cls, "class=\"", 7) != 0) cls--;
            const char *gt = strchr(cp, '>');
            if (!gt || gt >= rend) break;

            uint8_t fg = TTX_WHITE;
            uint8_t bg = TTX_BLACK;
            if (strncmp(cls, "class=\"", 7) == 0) {
                fg = wdr_parse_color(cls + 7, gt, false);
                bg = wdr_parse_color(cls + 7, gt, true);
            }

            const char *col_end = strstr(gt + 1, "<div ");
            if (!col_end || col_end > rend) col_end = rend;

            uint16_t cur_link = 0;
            const char *q = gt + 1;
            while (q < col_end && col < TTX_COLS) {
                if (*q == '<') {
                    if (strncmp(q, "<a ", 3) == 0) {
                        const char *pn = strstr(q, "page__num-");
                        const char *agt = strchr(q, '>');
                        if (pn && agt && pn < agt) cur_link = (uint16_t)atoi(pn + 10);
                        q = agt ? agt + 1 : q + 1;
                    } else if (strncmp(q, "</a>", 4) == 0) {
                        cur_link = 0;
                        q += 4;
                    } else {
                        const char *tgt = strchr(q, '>');
                        q = tgt ? tgt + 1 : q + 1;
                    }
                } else if (*q == '&') {
                    int cons = 1;
                    uint8_t ch = decode_html_entity(q, &cons);
                    q += cons;
                    out->cells[r][col].ch = ch;
                    out->cells[r][col].fg = fg;
                    out->cells[r][col].bg = bg;
                    out->cells[r][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                    out->cells[r][col].link_page = cur_link;
                    col++;
                } else if (*q == '\r' || *q == '\n' || *q == '\t') {
                    q++;
                } else {
                    int cons = 1;
                    uint8_t ch = utf8_to_latin1_char(q, &cons);
                    q += cons;
                    out->cells[r][col].ch = ch;
                    out->cells[r][col].fg = fg;
                    out->cells[r][col].bg = bg;
                    out->cells[r][col].attrs = cur_link ? TTX_ATTR_LINK : 0;
                    out->cells[r][col].link_page = cur_link;
                    col++;
                }
            }
            cp = strstr(gt + 1, "vt_col");
        }
    }

    if (parsed_rows == 0) return false;
    snprintf(out->title, sizeof(out->title), "WDR Text - Seite %d", out->page);
    ttx_page_detect_links(out);
    out->valid = true;
    return true;
}

/* ============================================================================
 * Live Fetch Dispatcher
 * ========================================================================== */

bool ttx_fetch_live_page(int channel_idx, int page, int subpage, TtxPage *out, char *err_buf, int err_len) {
    if (channel_idx < 0 || channel_idx >= TTX_MAX_CHANS) return false;
    if (page < 100 || page > 899) page = 100;
    if (subpage < 1) subpage = 1;

    const TtxChannel *ch = &g_ttx_channels[channel_idx];
    char url[512];

    if (ch->source == TTX_SRC_ARD) {
        snprintf(url, sizeof(url), "https://www.ard-text.de/page_only.php?page=%d&sub=%d", page, subpage);
    } else if (ch->source == TTX_SRC_HR) {
        snprintf(url, sizeof(url), "https://www.hr-text.hr-fernsehen.de/ttxweb/?page=%d&sub=%d", page, subpage);
    } else if (ch->source == TTX_SRC_SWR) {
        snprintf(url, sizeof(url), "https://wraps.swr.de/videotext/?stream=rp&page=%d&sub=%d", page, subpage);
    } else if (ch->source == TTX_SRC_WDR) {
        snprintf(url, sizeof(url), "https://www1.wdr.de/wdrtext/externvtx100~_eap__page__num-%d.html", page);
    } else {
        snprintf(url, sizeof(url), "https://teletext.zdf.de/teletext/%s/seiten/%d.html", ch->api_channel, page);
    }

    char *body = NULL;
    int body_len = 0;
    int status = 0;
    if (!http_get(url, &body, &body_len, &status, err_buf, err_len)) {
        if (body) free(body);
        if (ch->source == TTX_SRC_ZDF && status == 503 && err_buf) {
            snprintf(err_buf, err_len, "%s Teletext-Server z.Zt. im Wartungsmodus (HTTP 503)", ch->name);
        } else if (err_buf && err_buf[0] == '\0') {
            snprintf(err_buf, err_len, "HTTP %d beim Laden von Seite %d", status, page);
        }
        return false;
    }

    bool ok = false;
    if (ch->source == TTX_SRC_ARD) {
        ok = ttx_parse_ard_html(body, body_len, channel_idx, page, subpage, out);
    } else if (ch->source == TTX_SRC_HR || ch->source == TTX_SRC_SWR) {
        ok = ttx_parse_hr_html(body, body_len, channel_idx, page, subpage, out);
    } else if (ch->source == TTX_SRC_WDR) {
        ok = ttx_parse_wdr_html(body, body_len, channel_idx, page, subpage, out);
    }

    free(body);
    if (!ok && err_buf) {
        snprintf(err_buf, err_len, "Seite %d (%s) nicht verfuegbar", page, ch->name);
    }
    if (ok) {
        ttx_cache_store(out);
    }
    return ok;
}

/* ============================================================================
 * In-Memory LRU Cache & Full-Text Search
 * ========================================================================== */

typedef struct {
    TtxPage  page;
    uint32_t stamp;
    bool     used;
} TtxCacheEntry;

static TtxCacheEntry g_cache[TTX_CACHE_SIZE];
static uint32_t g_cache_clock = 1;

void ttx_cache_init(void) {
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_clock = 1;
}

void ttx_cache_store(const TtxPage *pg) {
    if (!pg || !pg->valid) return;

    int slot = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < TTX_CACHE_SIZE; i++) {
        if (g_cache[i].used &&
            g_cache[i].page.channel_idx == pg->channel_idx &&
            g_cache[i].page.page == pg->page &&
            g_cache[i].page.subpage == pg->subpage) {
            slot = i;
            break;
        }
        if (!g_cache[i].used && slot < 0) {
            slot = i;
        } else if (g_cache[i].used && g_cache[i].stamp < oldest) {
            oldest = g_cache[i].stamp;
            if (slot < 0) slot = i;
        }
    }
    if (slot < 0) slot = 0;

    g_cache[slot].page = *pg;
    g_cache[slot].stamp = g_cache_clock++;
    g_cache[slot].used = true;
}

bool ttx_cache_lookup(int channel_idx, int page, int subpage, TtxPage *out) {
    for (int i = 0; i < TTX_CACHE_SIZE; i++) {
        if (g_cache[i].used &&
            g_cache[i].page.channel_idx == channel_idx &&
            g_cache[i].page.page == page &&
            g_cache[i].page.subpage == subpage) {
            g_cache[i].stamp = g_cache_clock++;
            *out = g_cache[i].page;
            return true;
        }
    }
    return false;
}

int ttx_cache_count(void) {
    int c = 0;
    for (int i = 0; i < TTX_CACHE_SIZE; i++) {
        if (g_cache[i].used) c++;
    }
    return c;
}

static const char *strcasestr_local(const char *hay, const char *needle) {
    if (!*needle) return hay;
    size_t nlen = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == nlen) return p;
    }
    return NULL;
}

int ttx_cache_search(const char *query, int channel_filter, TtxSearchResult *results, int max_results) {
    if (!query || !*query || max_results <= 0) return 0;
    int found = 0;

    for (int i = 0; i < TTX_CACHE_SIZE && found < max_results; i++) {
        if (!g_cache[i].used) continue;
        const TtxPage *pg = &g_cache[i].page;
        if (channel_filter >= 0 && pg->channel_idx != channel_filter) continue;

        for (int r = 1; r < TTX_ROWS; r++) {
            char line[TTX_COLS + 1];
            for (int c = 0; c < TTX_COLS; c++) {
                uint8_t ch = pg->cells[r][c].ch;
                line[c] = (pg->cells[r][c].attrs & TTX_ATTR_MOSAIC) ? ' ' : (char)(ch >= 32 ? ch : ' ');
            }
            line[TTX_COLS] = '\0';

            if (strcasestr_local(line, query) != NULL) {
                results[found].channel_idx = pg->channel_idx;
                results[found].page = pg->page;
                results[found].subpage = pg->subpage;
                /* Trim leading spaces for snippet */
                const char *s = line;
                while (*s == ' ') s++;
                snprintf(results[found].snippet, sizeof(results[found].snippet), "%s", s);
                found++;
                break;
            }
        }
    }
    return found;
}

/* ============================================================================
 * Export Plain Text
 * ========================================================================== */

bool ttx_export_text(const TtxPage *pg, const char *filepath) {
    if (!pg || !filepath) return false;
    FILE *f = fopen(filepath, "w");
    if (!f) return false;

    fprintf(f, "=== %s - Seite %d (%d/%d) ===\n",
            g_ttx_channels[pg->channel_idx].name, pg->page, pg->subpage, pg->total_subpages);

    for (int r = 0; r < TTX_ROWS; r++) {
        char line[TTX_COLS + 1];
        int last_nonspace = -1;
        for (int c = 0; c < TTX_COLS; c++) {
            if (pg->cells[r][c].attrs & TTX_ATTR_MOSAIC) {
                line[c] = (pg->cells[r][c].ch != 0) ? '#' : ' ';
            } else {
                line[c] = (char)(pg->cells[r][c].ch >= 32 ? pg->cells[r][c].ch : ' ');
            }
            if (line[c] != ' ') last_nonspace = c;
        }
        line[last_nonspace + 1] = '\0';
        fprintf(f, "%s\n", line);
    }

    fclose(f);
    return true;
}
