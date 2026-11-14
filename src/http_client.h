#ifndef VORTEX_HTTP_CLIENT_H
#define VORTEX_HTTP_CLIENT_H

#include <stdbool.h>

/* Initialize bsdsocket.library + AmiSSL (call once at startup) */
bool http_init(char *err_buf, int err_len);
void http_cleanup(void);

/* Perform HTTPS or HTTP GET request. Caller must free(*out_body). */
bool http_get(const char *url, char **out_body, int *out_len, int *out_status, char *err_buf, int err_len);

bool http_has_ssl(void);

#endif /* VORTEX_HTTP_CLIENT_H */
