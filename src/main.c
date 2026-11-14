#include "http_client.h"
#include "charset.h"
#include "gui.h"

#include <stdio.h>
#include <stdlib.h>

static const char * const USED ver_tag = "$VER: VortexTeletext 1.0 (14.11.2026) AmigaOS 4.1 PowerPC - Copyright by TaKiTo";
static const char * const USED stack_cookie = "$STACK: 131072";

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    charset_init();

    char err[128] = {0};
    if (!http_init(err, sizeof(err))) {
        fprintf(stderr, "VortexTeletext: Network init failed: %s\n", err);
        charset_cleanup();
        return 10;
    }

    TtxApp *app = (TtxApp *)calloc(1, sizeof(TtxApp));
    if (!app) {
        http_cleanup();
        charset_cleanup();
        return 10;
    }

    if (gui_init(app)) {
        gui_run_loop(app);
    } else {
        fprintf(stderr, "VortexTeletext: Failed to initialize ReAction GUI.\n");
    }

    gui_cleanup(app);
    free(app);
    http_cleanup();
    charset_cleanup();
    return 0;
}
