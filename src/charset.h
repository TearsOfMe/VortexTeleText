#ifndef TTX_CHARSET_H
#define TTX_CHARSET_H

#include <exec/types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize codesets.library (v6, fallback v5) and obtain interface */
void charset_init(void);

/* Drop interface and close codesets.library */
void charset_cleanup(void);

/* Returns true if codesets.library is loaded and available */
bool charset_is_available(void);

/* Convert a UTF-8 string to Amiga local / ISO-8859-1 charset using codesets.library (with robust fallback) */
void charset_utf8_to_local(const char *utf8_str, char *dst, size_t dst_size);

/* Convert an Amiga local / ISO-8859-1 string to UTF-8 using codesets.library (with fallback) */
void charset_local_to_utf8(const char *local_str, char *dst, size_t dst_size);

/* Convert single UTF-8 sequence to ISO-8859-1/local byte, reporting consumed bytes */
uint8_t charset_utf8_char_to_local(const char *s, int *consumed);

#ifdef __cplusplus
}
#endif

#endif /* TTX_CHARSET_H */
