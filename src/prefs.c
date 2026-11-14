#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dos/dos.h>
#include <proto/dos.h>

static void ensure_envarc_dir(void) {
    BPTR lock = IDOS->Lock("ENVARC:VortexTeletext", SHARED_LOCK);
    if (lock) {
        IDOS->UnLock(lock);
        return;
    }
    lock = IDOS->CreateDir("ENVARC:VortexTeletext");
    if (lock) IDOS->UnLock(lock);
}

void prefs_init_defaults(TtxPrefs *p) {
    memset(p, 0, sizeof(*p));
    p->default_channel = 0; /* ARD Text */
    p->default_page = 100;
    p->reveal_mode = false;
    p->flash_enabled = true;
    p->fit_width = true;
    snprintf(p->export_dir, sizeof(p->export_dir), "RAM:");
}

void prefs_load(TtxPrefs *p) {
    prefs_init_defaults(p);
    FILE *f = fopen("ENVARC:VortexTeletext/prefs.cfg", "r");
    if (!f) return;

    char line[320];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *val = eq + 1;
        size_t len = strlen(val);
        while (len > 0 && (val[len - 1] == '\n' || val[len - 1] == '\r')) {
            val[--len] = '\0';
        }
        if (strcmp(line, "channel") == 0) {
            int ch = atoi(val);
            if (ch >= 0 && ch < TTX_MAX_CHANS) p->default_channel = ch;
        } else if (strcmp(line, "page") == 0) {
            int pg = atoi(val);
            if (pg >= 100 && pg <= 899) p->default_page = pg;
        } else if (strcmp(line, "reveal") == 0) {
            p->reveal_mode = (atoi(val) != 0);
        } else if (strcmp(line, "flash") == 0) {
            p->flash_enabled = (atoi(val) != 0);
        } else if (strcmp(line, "fit_width") == 0) {
            p->fit_width = (atoi(val) != 0);
        } else if (strcmp(line, "export_dir") == 0 && val[0]) {
            snprintf(p->export_dir, sizeof(p->export_dir), "%s", val);
        }
    }
    fclose(f);
}

void prefs_save(const TtxPrefs *p) {
    ensure_envarc_dir();
    FILE *f = fopen("ENVARC:VortexTeletext/prefs.cfg", "w");
    if (!f) return;
    fprintf(f, "channel=%d\n", p->default_channel);
    fprintf(f, "page=%d\n", p->default_page);
    fprintf(f, "reveal=%d\n", p->reveal_mode ? 1 : 0);
    fprintf(f, "flash=%d\n", p->flash_enabled ? 1 : 0);
    fprintf(f, "fit_width=%d\n", p->fit_width ? 1 : 0);
    fprintf(f, "export_dir=%s\n", p->export_dir);
    fclose(f);
}

void favs_load(TtxFavorite *favs, int *count, int max_favs) {
    *count = 0;
    FILE *f = fopen("ENVARC:VortexTeletext/favorites.cfg", "r");
    if (!f) {
        /* Default starter favorites */
        favs_add(favs, count, max_favs, 0, 100, "ARD Startseite");
        favs_add(favs, count, max_favs, 0, 101, "ARD Tagesschau");
        favs_add(favs, count, max_favs, 0, 200, "ARD Sport");
        favs_add(favs, count, max_favs, 3, 100, "HR Startseite");
        favs_add(favs, count, max_favs, 4, 100, "WDR Startseite");
        return;
    }

    char line[128];
    while (*count < max_favs && fgets(line, sizeof(line), f)) {
        int ch = 0, pg = 100;
        char label[48] = {0};
        if (sscanf(line, "%d|%d|%47[^\r\n]", &ch, &pg, label) >= 2) {
            if (ch >= 0 && ch < TTX_MAX_CHANS && pg >= 100 && pg <= 899) {
                favs[*count].channel_idx = ch;
                favs[*count].page = pg;
                snprintf(favs[*count].label, sizeof(favs[*count].label), "%s",
                         label[0] ? label : g_ttx_channels[ch].name);
                (*count)++;
            }
        }
    }
    fclose(f);
}

void favs_save(const TtxFavorite *favs, int count) {
    ensure_envarc_dir();
    FILE *f = fopen("ENVARC:VortexTeletext/favorites.cfg", "w");
    if (!f) return;
    for (int i = 0; i < count; i++) {
        fprintf(f, "%d|%d|%s\n", favs[i].channel_idx, favs[i].page, favs[i].label);
    }
    fclose(f);
}

bool favs_add(TtxFavorite *favs, int *count, int max_favs, int channel_idx, int page, const char *label) {
    for (int i = 0; i < *count; i++) {
        if (favs[i].channel_idx == channel_idx && favs[i].page == page) {
            return false;
        }
    }
    if (*count >= max_favs) return false;
    favs[*count].channel_idx = channel_idx;
    favs[*count].page = page;
    snprintf(favs[*count].label, sizeof(favs[*count].label), "%s", label);
    (*count)++;
    return true;
}

void favs_remove(TtxFavorite *favs, int *count, int index) {
    if (index < 0 || index >= *count) return;
    for (int i = index; i < *count - 1; i++) {
        favs[i] = favs[i + 1];
    }
    (*count)--;
}
