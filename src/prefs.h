#ifndef VORTEX_PREFS_H
#define VORTEX_PREFS_H

#include "teletext.h"
#include <stdbool.h>

typedef struct {
    int  default_channel;
    int  default_page;
    bool reveal_mode;
    bool flash_enabled;
    bool fit_width;
    char export_dir[256];
} TtxPrefs;

void prefs_init_defaults(TtxPrefs *p);
void prefs_load(TtxPrefs *p);
void prefs_save(const TtxPrefs *p);

void favs_load(TtxFavorite *favs, int *count, int max_favs);
void favs_save(const TtxFavorite *favs, int count);
bool favs_add(TtxFavorite *favs, int *count, int max_favs, int channel_idx, int page, const char *label);
void favs_remove(TtxFavorite *favs, int *count, int index);

#endif /* VORTEX_PREFS_H */
