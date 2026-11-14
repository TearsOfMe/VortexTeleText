#include "charset.h"

#include <proto/exec.h>
#include <proto/codesets.h>
#include <libraries/codesets.h>

#include <string.h>
#include <stdlib.h>

struct Library *CodesetsBase = NULL;
struct CodesetsIFace *ICodesets = NULL;

void charset_init(void) {
    if (!CodesetsBase) {
        CodesetsBase = IExec->OpenLibrary("codesets.library", 6);
        if (!CodesetsBase) {
            CodesetsBase = IExec->OpenLibrary("codesets.library", 5);
        }
    }
    if (CodesetsBase && !ICodesets) {
        ICodesets = (struct CodesetsIFace *)IExec->GetInterface(CodesetsBase, "main", 1, NULL);
    }
}

void charset_cleanup(void) {
    if (ICodesets) {
        IExec->DropInterface((struct Interface *)ICodesets);
        ICodesets = NULL;
    }
    if (CodesetsBase) {
        IExec->CloseLibrary(CodesetsBase);
        CodesetsBase = NULL;
    }
}

bool charset_is_available(void) {
    return (ICodesets != NULL);
}

void charset_utf8_to_local(const char *utf8_str, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!utf8_str || utf8_str[0] == '\0') return;

    if (ICodesets) {
        STRPTR res = ICodesets->CodesetsUTF8ToStr(
            CSA_Source,        (Tag)utf8_str,
            CSA_Dest,          (Tag)dst,
            CSA_DestLen,       (Tag)dst_size,
            CSA_DestMIBenum,   CS_MIBENUM_ISO_8859_1,
            CSA_AllocIfNeeded, FALSE,
            TAG_DONE);
        if (res) {
            dst[dst_size - 1] = '\0';
            return;
        }
    }

    /* Fallback UTF-8 to ISO-8859-1 (Latin-1) */
    const unsigned char *s = (const unsigned char *)utf8_str;
    size_t d = 0;
    while (*s && d + 1 < dst_size) {
        if (*s < 0x80) {
            dst[d++] = (char)*s++;
        } else if ((*s & 0xE0) == 0xC0 && *(s + 1)) {
            uint32_t cp = ((*s & 0x1F) << 6) | (*(s + 1) & 0x3F);
            s += 2;
            if (cp <= 0xFF) {
                dst[d++] = (char)(unsigned char)cp;
            } else {
                dst[d++] = '?';
            }
        } else if ((*s & 0xF0) == 0xE0 && *(s + 1) && *(s + 2)) {
            uint32_t cp = ((*s & 0x0F) << 12) | ((*(s + 1) & 0x3F) << 6) | (*(s + 2) & 0x3F);
            s += 3;
            if (cp == 0x20AC) {
                dst[d++] = 'E'; /* Euro fallback */
            } else if (cp == 0x2013 || cp == 0x2014) {
                dst[d++] = '-';
            } else if (cp == 0x2018 || cp == 0x2019) {
                dst[d++] = '\'';
            } else if (cp == 0x201C || cp == 0x201D) {
                dst[d++] = '"';
            } else if (cp == 0x2026) {
                if (d + 3 < dst_size) {
                    dst[d++] = '.'; dst[d++] = '.'; dst[d++] = '.';
                } else {
                    dst[d++] = '.';
                }
            } else {
                dst[d++] = ' ';
            }
        } else if ((*s & 0xF8) == 0xF0 && *(s + 1) && *(s + 2) && *(s + 3)) {
            s += 4;
            dst[d++] = ' ';
        } else {
            s++;
        }
    }
    dst[d] = '\0';
}

void charset_local_to_utf8(const char *local_str, char *dst, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!local_str || local_str[0] == '\0') return;

    if (ICodesets) {
        UTF8 *res = ICodesets->CodesetsUTF8Create(
            CSA_Source,        (Tag)local_str,
            CSA_Dest,          (Tag)dst,
            CSA_DestLen,       (Tag)dst_size,
            CSA_SourceMIBenum, CS_MIBENUM_ISO_8859_1,
            CSA_AllocIfNeeded, FALSE,
            TAG_DONE);
        if (res) {
            dst[dst_size - 1] = '\0';
            return;
        }
    }

    /* Fallback ISO-8859-1 to UTF-8 */
    const unsigned char *s = (const unsigned char *)local_str;
    size_t d = 0;
    while (*s && d + 1 < dst_size) {
        if (*s < 0x80) {
            dst[d++] = (char)*s++;
        } else {
            if (d + 2 >= dst_size) break;
            dst[d++] = (char)(0xC0 | (*s >> 6));
            dst[d++] = (char)(0x80 | (*s & 0x3F));
            s++;
        }
    }
    dst[d] = '\0';
}

uint8_t charset_utf8_char_to_local(const char *s, int *consumed) {
    unsigned char b0 = (unsigned char)s[0];
    if (b0 < 0x80) {
        *consumed = 1;
        return b0;
    }

    if ((b0 & 0xE0) == 0xC0 && s[1]) {
        *consumed = 2;
        if (ICodesets) {
            char in[3] = { s[0], s[1], '\0' };
            char out[4] = { 0 };
            STRPTR res = ICodesets->CodesetsUTF8ToStr(
                CSA_Source,        (Tag)in,
                CSA_Dest,          (Tag)out,
                CSA_DestLen,       sizeof(out),
                CSA_DestMIBenum,   CS_MIBENUM_ISO_8859_1,
                CSA_AllocIfNeeded, FALSE,
                TAG_DONE);
            if (res && out[0]) return (uint8_t)out[0];
        }
        uint16_t cp = (uint16_t)(((b0 & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F));
        return (cp <= 0xFF) ? (uint8_t)cp : '?';
    }

    if ((b0 & 0xF0) == 0xE0 && s[1] && s[2]) {
        *consumed = 3;
        if (ICodesets) {
            char in[4] = { s[0], s[1], s[2], '\0' };
            char out[4] = { 0 };
            STRPTR res = ICodesets->CodesetsUTF8ToStr(
                CSA_Source,        (Tag)in,
                CSA_Dest,          (Tag)out,
                CSA_DestLen,       sizeof(out),
                CSA_DestMIBenum,   CS_MIBENUM_ISO_8859_1,
                CSA_AllocIfNeeded, FALSE,
                TAG_DONE);
            if (res && out[0]) return (uint8_t)out[0];
        }
        uint32_t cp = (((b0 & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F));
        if (cp == 0x2013 || cp == 0x2014) return '-';
        if (cp == 0x2018 || cp == 0x2019) return '\'';
        if (cp == 0x201C || cp == 0x201D) return '"';
        if (cp == 0x2026) return '.';
        if (cp == 0x20AC) return 'E';
        return ' ';
    }

    if ((b0 & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
        *consumed = 4;
        return ' ';
    }

    *consumed = 1;
    return '?';
}
