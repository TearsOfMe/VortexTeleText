#define __USE_INLINE__

#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/tags.h>
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

struct Library *SocketBase = NULL;
struct SocketIFace *ISocket = NULL;

struct Library *AmiSSLMasterBase = NULL;
struct AmiSSLMasterIFace *IAmiSSLMaster = NULL;

struct Library *AmiSSLBase = NULL;
struct Library *AmiSSLExtBase = NULL;
struct AmiSSLIFace *IAmiSSL = NULL;

static SSL_CTX *g_ssl_ctx = NULL;
static bool g_http_initialized = false;

bool http_has_ssl(void) {
    return (g_ssl_ctx != NULL && IAmiSSL != NULL);
}

bool http_init(char *err_buf, int err_len) {
    if (g_http_initialized) return true;

    SocketBase = OpenLibrary("bsdsocket.library", 4);
    if (!SocketBase) {
        if (err_buf) snprintf(err_buf, err_len, "Cannot open bsdsocket.library v4");
        return false;
    }
    ISocket = (struct SocketIFace *)GetInterface(SocketBase, "main", 1, NULL);
    if (!ISocket) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        if (err_buf) snprintf(err_buf, err_len, "Cannot get bsdsocket main interface");
        return false;
    }

    /* Try AmiSSL 5.1+ first, then fallback to AmiSSL 4.x */
    AmiSSLMasterBase = OpenLibrary("amisslmaster.library", 5);
    if (!AmiSSLMasterBase) {
        AmiSSLMasterBase = OpenLibrary("amisslmaster.library", 4);
    }

    if (AmiSSLMasterBase) {
        IAmiSSLMaster = (struct AmiSSLMasterIFace *)GetInterface(AmiSSLMasterBase, "main", 1, NULL);
        if (IAmiSSLMaster) {
            LONG ver = (AmiSSLMasterBase->lib_Version >= 5) ? AMISSL_V303 : AMISSL_V111d;
            if (OpenAmiSSLTags(ver,
                               AmiSSL_UsesOpenSSLStructs, FALSE,
                               AmiSSL_GetAmiSSLBase,      (ULONG)&AmiSSLBase,
                               AmiSSL_GetAmiSSLExtBase,   (ULONG)&AmiSSLExtBase,
                               AmiSSL_GetIAmiSSL,         (ULONG)&IAmiSSL,
                               AmiSSL_ISocket,            (ULONG)ISocket,
                               AmiSSL_ErrNoPtr,           (ULONG)&errno,
                               TAG_DONE) == 0) {
                g_ssl_ctx = SSL_CTX_new(TLS_client_method());
                if (g_ssl_ctx) {
                    SSL_CTX_set_default_verify_paths(g_ssl_ctx);
                    /* Allow connections even if AmigaOS root CA bundle is slightly outdated */
                    SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_NONE, NULL);
                }
            }
        }
    }

    g_http_initialized = true;
    if (!g_ssl_ctx && err_buf) {
        snprintf(err_buf, err_len, "Warning: AmiSSL unavailable (HTTP only)");
    }
    return true;
}

void http_cleanup(void) {
    if (g_ssl_ctx && IAmiSSL) {
        SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = NULL;
    }
    if (IAmiSSLMaster && AmiSSLBase) {
        CloseAmiSSL();
        AmiSSLBase = NULL;
        AmiSSLExtBase = NULL;
        IAmiSSL = NULL;
    }
    if (IAmiSSLMaster) {
        DropInterface((struct Interface *)IAmiSSLMaster);
        IAmiSSLMaster = NULL;
    }
    if (AmiSSLMasterBase) {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }
    if (ISocket) {
        DropInterface((struct Interface *)ISocket);
        ISocket = NULL;
    }
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
    g_http_initialized = false;
}

static bool parse_url(const char *url, bool *is_https, char *host, int host_sz, int *port, char *path, int path_sz) {
    const char *p = url;
    if (strncmp(p, "https://", 8) == 0) {
        *is_https = true;
        *port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        *is_https = false;
        *port = 80;
        p += 7;
    } else {
        return false;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    int host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = (int)(colon - p);
        *port = atoi(colon + 1);
        if (*port <= 0) *port = *is_https ? 443 : 80;
    } else if (slash) {
        host_len = (int)(slash - p);
    } else {
        host_len = (int)strlen(p);
    }

    if (host_len <= 0 || host_len >= host_sz) return false;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    if (slash) {
        snprintf(path, path_sz, "%s", slash);
    } else {
        snprintf(path, path_sz, "/");
    }
    return true;
}

static int strncasecmp_local(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
        unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
        if (ca != cb || ca == 0) return (int)ca - (int)cb;
    }
    return 0;
}

static char *decode_chunked(const char *src, int src_len, int *out_len) {
    char *dst = (char *)malloc((size_t)src_len + 1);
    if (!dst) return NULL;
    int pos = 0;
    int dpos = 0;

    while (pos < src_len) {
        int line_start = pos;
        while (pos < src_len && src[pos] != '\n') pos++;
        if (pos >= src_len) break;
        int line_len = pos - line_start;
        pos++; /* skip '\n' */

        char hexbuf[32];
        int copy_len = line_len < 31 ? line_len : 31;
        memcpy(hexbuf, src + line_start, copy_len);
        hexbuf[copy_len] = '\0';
        char *semi = strchr(hexbuf, ';');
        if (semi) *semi = '\0';

        long chunk_sz = strtol(hexbuf, NULL, 16);
        if (chunk_sz <= 0) break;
        if (pos + chunk_sz > src_len) {
            chunk_sz = src_len - pos;
        }
        memcpy(dst + dpos, src + pos, (size_t)chunk_sz);
        dpos += (int)chunk_sz;
        pos += (int)chunk_sz;

        /* Skip CRLF after chunk */
        if (pos < src_len && src[pos] == '\r') pos++;
        if (pos < src_len && src[pos] == '\n') pos++;
    }

    dst[dpos] = '\0';
    *out_len = dpos;
    return dst;
}

static bool http_get_once(const char *url, char **out_body, int *out_len, int *out_status, char *redirect_url, int redir_sz, char *err_buf, int err_len) {
    bool is_https = false;
    char host[256];
    char path[1024];
    int port = 80;

    if (redirect_url) redirect_url[0] = '\0';

    if (!parse_url(url, &is_https, host, sizeof(host), &port, path, sizeof(path))) {
        if (err_buf) snprintf(err_buf, err_len, "Invalid URL: %s", url);
        return false;
    }

    if (is_https && !http_has_ssl()) {
        if (err_buf) snprintf(err_buf, err_len, "AmiSSL 4/5 required for HTTPS");
        return false;
    }

    struct hostent *he = gethostbyname((STRPTR)host);
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        if (err_buf) snprintf(err_buf, err_len, "DNS lookup failed for %s", host);
        return false;
    }

    LONG sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        if (err_buf) snprintf(err_buf, err_len, "socket() failed");
        return false;
    }

    /* 10 second timeout on send/recv */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], (size_t)he->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        CloseSocket(sock);
        if (err_buf) snprintf(err_buf, err_len, "Connect failed to %s:%d", host, port);
        return false;
    }

    SSL *ssl = NULL;
    if (is_https) {
        ssl = SSL_new(g_ssl_ctx);
        if (!ssl) {
            CloseSocket(sock);
            if (err_buf) snprintf(err_buf, err_len, "SSL_new() failed");
            return false;
        }
        SSL_set_fd(ssl, (int)sock);
        SSL_ctrl(ssl, SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name, (void *)host);
        if (SSL_connect(ssl) <= 0) {
            SSL_free(ssl);
            CloseSocket(sock);
            if (err_buf) snprintf(err_buf, err_len, "TLS handshake failed (%s)", host);
            return false;
        }
    }

    char req[1600];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: VortexTeletext/1.0 (AmigaOS 4.1 PPC)\r\n"
        "Accept: application/json, text/html, */*\r\n"
        "Accept-Encoding: identity\r\n"
        "Connection: close\r\n\r\n",
        path, host);

    int sent = 0;
    while (sent < req_len) {
        int n = is_https ? SSL_write(ssl, req + sent, req_len - sent)
                         : send(sock, (APTR)(req + sent), req_len - sent, 0);
        if (n <= 0) {
            if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
            CloseSocket(sock);
            if (err_buf) snprintf(err_buf, err_len, "HTTP send failed");
            return false;
        }
        sent += n;
    }

    int cap = 32768;
    int total = 0;
    char *buf = (char *)malloc((size_t)cap);
    if (!buf) {
        if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
        CloseSocket(sock);
        return false;
    }

    for (;;) {
        if (total + 8192 + 1 > cap) {
            if (cap >= 4 * 1024 * 1024) break; /* safety limit 4 MB */
            cap *= 2;
            char *nb = (char *)realloc(buf, (size_t)cap);
            if (!nb) break;
            buf = nb;
        }
        int n = is_https ? SSL_read(ssl, buf + total, 8192)
                         : recv(sock, (APTR)(buf + total), 8192, 0);
        if (n <= 0) break;
        total += n;
    }
    buf[total] = '\0';

    if (ssl) {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    CloseSocket(sock);

    char *hdr_end = strstr(buf, "\r\n\r\n");
    int sep_len = 4;
    if (!hdr_end) {
        hdr_end = strstr(buf, "\n\n");
        sep_len = 2;
    }
    if (!hdr_end) {
        free(buf);
        if (err_buf) snprintf(err_buf, err_len, "Malformed HTTP response");
        return false;
    }

    int status = 0;
    const char *sp = strchr(buf, ' ');
    if (sp && sp < hdr_end) status = atoi(sp + 1);
    if (out_status) *out_status = status;

    /* Check headers */
    bool is_chunked = false;
    int hdr_len = (int)(hdr_end - buf);
    for (int i = 0; i < hdr_len; i++) {
        if (i == 0 || buf[i - 1] == '\n') {
            if (strncasecmp_local(buf + i, "Transfer-Encoding:", 18) == 0) {
                const char *v = buf + i + 18;
                while (*v == ' ' || *v == '\t') v++;
                if (strncasecmp_local(v, "chunked", 7) == 0) is_chunked = true;
            } else if (redirect_url && strncasecmp_local(buf + i, "Location:", 9) == 0) {
                const char *v = buf + i + 9;
                while (*v == ' ' || *v == '\t') v++;
                int j = 0;
                while (v[j] && v[j] != '\r' && v[j] != '\n' && j < redir_sz - 1) {
                    redirect_url[j] = v[j];
                    j++;
                }
                redirect_url[j] = '\0';
            }
        }
    }

    char *body_ptr = hdr_end + sep_len;
    int body_len = total - (int)(body_ptr - buf);
    if (body_len < 0) body_len = 0;

    if (is_chunked) {
        char *decoded = decode_chunked(body_ptr, body_len, &body_len);
        free(buf);
        if (!decoded) return false;
        *out_body = decoded;
        *out_len = body_len;
    } else {
        char *copy = (char *)malloc((size_t)body_len + 1);
        if (!copy) { free(buf); return false; }
        memcpy(copy, body_ptr, (size_t)body_len);
        copy[body_len] = '\0';
        free(buf);
        *out_body = copy;
        *out_len = body_len;
    }

    return true;
}

bool http_get(const char *url, char **out_body, int *out_len, int *out_status, char *err_buf, int err_len) {
    char cur_url[1280];
    snprintf(cur_url, sizeof(cur_url), "%s", url);

    for (int hop = 0; hop < 4; hop++) {
        char redir[1024] = {0};
        int status = 0;
        char *body = NULL;
        int len = 0;

        if (!http_get_once(cur_url, &body, &len, &status, redir, sizeof(redir), err_buf, err_len)) {
            return false;
        }

        if ((status == 301 || status == 302 || status == 307 || status == 308) && redir[0]) {
            free(body);
            if (strncmp(redir, "http://", 7) == 0 || strncmp(redir, "https://", 8) == 0) {
                snprintf(cur_url, sizeof(cur_url), "%s", redir);
            } else {
                /* Relative redirect */
                bool is_https = false;
                char host[256], path[1024];
                int port = 80;
                if (parse_url(cur_url, &is_https, host, sizeof(host), &port, path, sizeof(path))) {
                    snprintf(cur_url, sizeof(cur_url), "%s://%s%s%s",
                             is_https ? "https" : "http",
                             host,
                             (redir[0] == '/') ? "" : "/",
                             redir);
                }
            }
            continue;
        }

        *out_body = body;
        *out_len = len;
        if (out_status) *out_status = status;
        return (status >= 200 && status < 400);
    }

    if (err_buf) snprintf(err_buf, err_len, "Too many HTTP redirects");
    return false;
}
