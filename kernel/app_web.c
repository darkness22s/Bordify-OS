#include "gui.h"
#include "gfx.h"
#include "settings.h"
#include "icons.h"
#include "ui.h"
#include "input.h"
#include "fat16.h"
#include "net.h"
#include "string.h"
#include "printf.h"

#define WEB_PAGE_SIZE 65536
#define WEB_URL_SIZE 192
#define WEB_HISTORY 12
#define WEB_MAX_LINKS 160

typedef struct {
    char href[WEB_URL_SIZE];
    int x;
    int y;
    int w;
    int h;
} web_link_t;

typedef struct {
    char url[WEB_URL_SIZE];
    char page[WEB_PAGE_SIZE];
    int page_length;
    int scroll;
    int content_height;
    int page_height;
    web_link_t links[WEB_MAX_LINKS];
    int link_count;
    char history[WEB_HISTORY][WEB_URL_SIZE];
    int history_count;
    int history_index;
    bool editing;
    char edit[WEB_URL_SIZE];
    char status[128];
    int load_error;
    int last_layout_width;
} web_state_t;

typedef struct {
    web_state_t *ws;
    int page_left;
    int page_top;
    int page_width;
    int page_height;
    int x;
    int y;
    int scroll;
    int content_height;
    const font_t *font;
    bool bold;
    bool href_active;
    char href[WEB_URL_SIZE];
    int skip_tag;
    char skip_name[16];
} html_ctx_t;

static web_state_t web;

static const char *home_fallback =
    "<html><head><title>Bordify Web</title></head><body>"
    "<h1>Bordify Web</h1>"
    "<p>The local start page could not be read from the FAT16 disk.</p>"
    "<p>Plug in the Bordify disk image and try again.</p>"
    "</body></html>";

static void web_navigate(web_state_t *ws, const char *url, bool record_history);

static void draw_icon_button(int x, int y, int kind, bool hover) {
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    if (hover) {
        gfx_rounded_fill(x, y, 30, 30, 7, settings_panel_alt());
    }
    gfx_rounded_outline(x, y, 30, 30, 7, 1, settings_line(), settings_panel());
    uint32_t ink = settings_ink();
    int cx = x + 15;
    int cy = y + 15;
    if (kind == 0 || kind == 1) {
        int dir = kind == 0 ? -1 : 1;
        gfx_line_thick(cx + dir * 4, cy - 6, cx + dir * -2, cy, 2, ink);
        gfx_line_thick(cx + dir * -2, cy, cx + dir * 4, cy + 6, 2, ink);
    } else if (kind == 2) {
        gfx_circle_outline(cx, cy, 7, 2, ink);
        gfx_line(cx + 3, cy - 8, cx + 8, cy - 3, ink);
        gfx_line(cx + 8, cy - 3, cx + 5, cy - 2, ink);
    } else {
        gfx_line_thick(cx - 7, cy + 2, cx, cy - 6, 2, ink);
        gfx_line_thick(cx, cy - 6, cx + 7, cy + 2, 2, ink);
        gfx_line_thick(cx - 6, cy + 2, cx + 6, cy + 2, 2, ink);
    }
    (void)mouse;
}

static void html_emit_space(html_ctx_t *ctx) {
    int space = gfx_text_width(ctx->font, " ");
    ctx->x += space;
}

static void html_newline(html_ctx_t *ctx) {
    ctx->x = ctx->page_left;
    ctx->y += ctx->font->line_height + 3;
}

static void html_draw_word(html_ctx_t *ctx, const char *word) {
    if (!word || !word[0]) {
        return;
    }
    uint32_t color = ctx->href_active ? settings_accent() : settings_ink();
    int width = gfx_text_width(ctx->font, word);
    if (ctx->x > ctx->page_left && ctx->x + width > ctx->page_left + ctx->page_width) {
        html_newline(ctx);
    }
    if (width > ctx->page_width) {
        width = ctx->page_width;
    }
    int screen_y = ctx->page_top + ctx->y - ctx->scroll;
    if (screen_y + ctx->font->line_height >= ctx->page_top &&
        screen_y <= ctx->page_top + ctx->page_height) {
        gfx_text_clip(ctx->font, ctx->x, screen_y, word, width, color);
        if (ctx->href_active) {
            gfx_hline(ctx->x, ctx->x + width, screen_y + ctx->font->line_height - 2,
                      settings_accent());
            if (ctx->ws->link_count < WEB_MAX_LINKS) {
                web_link_t *link = &ctx->ws->links[ctx->ws->link_count++];
                kstrncpy(link->href, ctx->href, sizeof(link->href) - 1);
                link->href[sizeof(link->href) - 1] = 0;
                link->x = ctx->x;
                link->y = screen_y;
                link->w = width;
                link->h = ctx->font->line_height;
            }
        }
    }
    ctx->x += width;
    if (ctx->y + ctx->font->line_height > ctx->content_height) {
        ctx->content_height = ctx->y + ctx->font->line_height;
    }
}

static void html_flush(html_ctx_t *ctx, const char *text) {
    if (!text) {
        return;
    }
    while (*text) {
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
            text++;
        }
        if (!*text) {
            break;
        }
        char word[192];
        int n = 0;
        while (*text && *text != ' ' && *text != '\t' && *text != '\r' &&
               *text != '\n' && n < 191) {
            word[n++] = *text++;
        }
        word[n] = 0;
        if (ctx->x > ctx->page_left) {
            html_emit_space(ctx);
            if (ctx->x + gfx_text_width(ctx->font, word) > ctx->page_left + ctx->page_width) {
                html_newline(ctx);
            }
        }
        html_draw_word(ctx, word);
    }
}

static bool tag_is(const char *name, const char *test) {
    return kstricmp(name, test) == 0;
}

static void extract_href(const char *tag_body, char *out, size_t out_size) {
    out[0] = 0;
    const char *p = tag_body;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        const char *key = p;
        while (*p && *p != '=' && *p != ' ' && *p != '>') {
            p++;
        }
        size_t key_len = (size_t)(p - key);
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '=') {
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            char quote = 0;
            if (*p == '"' || *p == '\'') {
                quote = *p++;
            }
            const char *value = p;
            if (quote) {
                while (*p && *p != quote) {
                    p++;
                }
            } else {
                while (*p && *p != ' ' && *p != '>') {
                    p++;
                }
            }
            if (key_len == 4 && kstrncmp(key, "href", 4) == 0) {
                size_t len = (size_t)(p - value);
                if (len >= out_size) {
                    len = out_size - 1;
                }
                kmemcpy(out, value, len);
                out[len] = 0;
                return;
            }
            if (quote && *p) {
                p++;
            }
        } else {
            while (*p && *p != ' ' && *p != '>') {
                p++;
            }
        }
    }
}

static void html_render(web_state_t *ws, const char *html, int px, int py, int pw, int ph) {
    html_ctx_t ctx;
    kmemset(&ctx, 0, sizeof(ctx));
    ctx.ws = ws;
    ctx.page_left = px + 30;
    ctx.page_top = py;
    ctx.page_width = pw - 60;
    ctx.page_height = ph;
    ctx.x = ctx.page_left;
    ctx.y = 26;
    ctx.font = &font_ui;
    ctx.content_height = ctx.y;
    ws->link_count = 0;

    char text[256];
    int text_len = 0;
    const char *p = html;
    while (*p) {
        if (ctx.skip_tag) {
            if (*p == '<' && p[1] == '/') {
                const char *q = p + 2;
                int n = 0;
                char closing[16];
                while (*q && *q != '>' && *q != ' ' && n < 15) {
                    closing[n++] = *q++;
                }
                closing[n] = 0;
                while (*q && *q != '>') {
                    q++;
                }
                if (*q == '>') {
                    q++;
                }
                p = q;
                if (tag_is(closing, ctx.skip_name)) {
                    ctx.skip_tag = 0;
                }
                continue;
            }
            p++;
            continue;
        }
        if (*p == '<') {
            if (text_len) {
                text[text_len] = 0;
                html_flush(&ctx, text);
                text_len = 0;
            }
            if (p[1] == '!' && p[2] == '-' && p[3] == '-') {
                const char *end = kstrstr(p + 4, "-->");
                p = end ? end + 3 : p + kstrlen(p);
                continue;
            }
            if (p[1] == '!' || p[1] == '?') {
                const char *end = kstrchr(p, '>');
                p = end ? end + 1 : p + kstrlen(p);
                continue;
            }
            bool closing = p[1] == '/';
            const char *q = p + (closing ? 2 : 1);
            char name[24];
            int n = 0;
            while (*q && *q != '>' && *q != ' ' && *q != '\t' && *q != '/' && n < 23) {
                name[n++] = *q++;
            }
            name[n] = 0;
            const char *body_start = q;
            while (*q && *q != '>') {
                q++;
            }
            const char *tag_end = q;
            if (*q == '>') {
                q++;
            }
            char tag_body[256];
            int body_len = (int)(tag_end - body_start);
            if (body_len > 255) {
                body_len = 255;
            }
            kmemcpy(tag_body, body_start, (size_t)body_len);
            tag_body[body_len] = 0;

            if (!closing && (tag_is(name, "script") || tag_is(name, "style") ||
                             tag_is(name, "title") || tag_is(name, "head"))) {
                kstrncpy(ctx.skip_name, name, sizeof(ctx.skip_name) - 1);
                ctx.skip_name[sizeof(ctx.skip_name) - 1] = 0;
                ctx.skip_tag = 1;
                p = q;
                continue;
            }

            if (tag_is(name, "h1")) {
                html_flush(&ctx, NULLPTR);
                html_newline(&ctx);
                ctx.y += 10;
                ctx.font = &font_h2;
                ctx.bold = true;
            } else if (tag_is(name, "h2")) {
                html_flush(&ctx, NULLPTR);
                html_newline(&ctx);
                ctx.y += 6;
                ctx.font = &font_title;
                ctx.bold = true;
            } else if (tag_is(name, "p") || tag_is(name, "div") || tag_is(name, "section") ||
                       tag_is(name, "article") || tag_is(name, "header") ||
                       tag_is(name, "footer")) {
                html_flush(&ctx, NULLPTR);
                ctx.y += 8;
                ctx.font = &font_ui;
                ctx.bold = false;
            } else if (tag_is(name, "li")) {
                html_flush(&ctx, NULLPTR);
                html_newline(&ctx);
                gfx_text(&font_ui, ctx.page_left, ctx.page_top + ctx.y - ctx.scroll, "-",
                         settings_muted());
                ctx.x = ctx.page_left + 18;
                ctx.font = &font_ui;
            } else if (tag_is(name, "br")) {
                html_flush(&ctx, NULLPTR);
                html_newline(&ctx);
            } else if (tag_is(name, "hr")) {
                html_flush(&ctx, NULLPTR);
                ctx.y += 10;
                gfx_hline(ctx.page_left, ctx.page_left + ctx.page_width,
                          ctx.page_top + ctx.y - ctx.scroll, settings_line());
                ctx.y += 14;
            } else if (tag_is(name, "a")) {
                html_flush(&ctx, NULLPTR);
                ctx.href_active = true;
                extract_href(tag_body, ctx.href, sizeof(ctx.href));
            } else if (tag_is(name, "b") || tag_is(name, "strong")) {
                html_flush(&ctx, NULLPTR);
                ctx.bold = true;
                if (ctx.font == &font_ui) {
                    ctx.font = &font_ui_bold;
                }
            } else if (tag_is(name, "img")) {
                char alt[WEB_URL_SIZE];
                alt[0] = 0;
                const char *ap = kstrstr(tag_body, "alt=");
                if (ap) {
                    ap += 4;
                    char quote = 0;
                    if (*ap == '"' || *ap == '\'') {
                        quote = *ap++;
                    }
                    int ai = 0;
                    while (*ap && ((quote && *ap != quote) || (!quote && *ap != ' ' && *ap != '>')) &&
                           ai < (int)sizeof(alt) - 1) {
                        alt[ai++] = *ap++;
                    }
                    alt[ai] = 0;
                }
                char placeholder[64];
                snprintf(placeholder, sizeof(placeholder), "[%s]",
                         alt[0] ? alt : "image");
                html_flush(&ctx, placeholder);
            } else if (closing) {
                if (tag_is(name, "h1") || tag_is(name, "h2")) {
                    html_flush(&ctx, NULLPTR);
                    html_newline(&ctx);
                    ctx.y += 10;
                    ctx.font = &font_ui;
                    ctx.bold = false;
                } else if (tag_is(name, "p") || tag_is(name, "div") ||
                           tag_is(name, "section") || tag_is(name, "article") ||
                           tag_is(name, "header") || tag_is(name, "footer")) {
                    html_flush(&ctx, NULLPTR);
                    html_newline(&ctx);
                    ctx.y += 4;
                    ctx.font = &font_ui;
                    ctx.bold = false;
                } else if (tag_is(name, "a")) {
                    html_flush(&ctx, NULLPTR);
                    ctx.href_active = false;
                    ctx.href[0] = 0;
                } else if (tag_is(name, "b") || tag_is(name, "strong")) {
                    html_flush(&ctx, NULLPTR);
                    ctx.bold = false;
                    if (ctx.font == &font_ui_bold) {
                        ctx.font = &font_ui;
                    }
                } else if (tag_is(name, "li")) {
                    html_flush(&ctx, NULLPTR);
                    html_newline(&ctx);
                }
            }
            p = q;
            continue;
        }
        if (*p == '&') {
            if (kstrncmp(p, "&amp;", 5) == 0) {
                if (text_len < 250) text[text_len++] = '&';
                p += 5;
                continue;
            }
            if (kstrncmp(p, "&lt;", 4) == 0) {
                if (text_len < 250) text[text_len++] = '<';
                p += 4;
                continue;
            }
            if (kstrncmp(p, "&gt;", 4) == 0) {
                if (text_len < 250) text[text_len++] = '>';
                p += 4;
                continue;
            }
            if (kstrncmp(p, "&quot;", 6) == 0) {
                if (text_len < 250) text[text_len++] = '"';
                p += 6;
                continue;
            }
            if (kstrncmp(p, "&#39;", 5) == 0 || kstrncmp(p, "&apos;", 6) == 0) {
                if (text_len < 250) text[text_len++] = '\'';
                p += p[1] == '#' ? 5 : 6;
                continue;
            }
            if (kstrncmp(p, "&nbsp;", 6) == 0) {
                if (text_len < 250) text[text_len++] = ' ';
                p += 6;
                continue;
            }
            if (text_len < 250) text[text_len++] = *p++;
            continue;
        }
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            if (text_len && text[text_len - 1] != ' ') {
                text[text_len++] = ' ';
            }
            p++;
            continue;
        }
        if (text_len < 250) {
            text[text_len++] = *p++;
        } else {
            text[text_len] = 0;
            html_flush(&ctx, text);
            text_len = 0;
        }
    }
    if (text_len) {
        text[text_len] = 0;
        html_flush(&ctx, text);
        ctx.y += ctx.font->line_height;
    }
    ws->content_height = ctx.content_height + 26;
    if (ws->content_height < ph) {
        ws->content_height = ph;
    }
}

static const char *local_path_for_url(const char *url) {
    if (kstricmp(url, "bordify://home") == 0) {
        return "WEB/HOME.HTM";
    }
    if (kstricmp(url, "bordify://guide") == 0) {
        return "WEB/GUIDE.HTM";
    }
    if (kstricmp(url, "bordify://about") == 0) {
        return "WEB/ABOUT.HTM";
    }
    return NULLPTR;
}

static void set_error_page(web_state_t *ws, const char *title, const char *message) {
    snprintf(ws->page, WEB_PAGE_SIZE,
             "<html><body><h1>%s</h1><p>%s</p>"
             "<hr><p><a href=\"bordify://home\">Return to the Bordify start page</a></p>"
             "</body></html>", title, message);
    ws->page_length = (int)kstrlen(ws->page);
    ws->load_error = 1;
}

static void web_push_history(web_state_t *ws, const char *url) {
    if (ws->history_count > 0 &&
        kstricmp(ws->history[ws->history_index], url) == 0) {
        return;
    }
    if (ws->history_index + 1 < ws->history_count) {
        ws->history_count = ws->history_index + 1;
    }
    if (ws->history_count < WEB_HISTORY) {
        kstrncpy(ws->history[ws->history_count], url, WEB_URL_SIZE - 1);
        ws->history[ws->history_count][WEB_URL_SIZE - 1] = 0;
        ws->history_index = ws->history_count;
        ws->history_count++;
    } else {
        for (int i = 1; i < WEB_HISTORY; i++) {
            kstrcpy(ws->history[i - 1], ws->history[i]);
        }
        kstrncpy(ws->history[WEB_HISTORY - 1], url, WEB_URL_SIZE - 1);
        ws->history[WEB_HISTORY - 1][WEB_URL_SIZE - 1] = 0;
        ws->history_index = WEB_HISTORY - 1;
    }
}

static bool web_scheme(const char *url, const char *scheme) {
    while (*scheme) {
        char a = *url++;
        char b = *scheme++;
        if (a >= 'A' && a <= 'Z') {
            a = (char)(a + ('a' - 'A'));
        }
        if (b >= 'A' && b <= 'Z') {
            b = (char)(b + ('a' - 'A'));
        }
        if (!a || a != b) {
            return false;
        }
    }
    return true;
}

static bool web_make_url(bool secure, const char *host, uint16_t port,
                         const char *path, char *out, size_t out_size) {
    const char *scheme = secure ? "https" : "http";
    uint16_t default_port = secure ? 443 : 80;
    int n;
    if (port == default_port) {
        n = snprintf(out, out_size, "%s://%s%s", scheme, host, path);
    } else {
        n = snprintf(out, out_size, "%s://%s:%u%s", scheme, host, port, path);
    }
    return n > 0 && (size_t)n < out_size;
}

static bool web_parse_url(const char *url, char *host, size_t host_size,
                          uint16_t *port, char *path, size_t path_size,
                          bool *secure) {
    bool is_https = web_scheme(url, "https://");
    if (!is_https && !web_scheme(url, "http://")) {
        return false;
    }
    const char *p = url + (is_https ? 8 : 7);
    *secure = is_https;
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != '#') {
        p++;
    }
    size_t host_len = (size_t)(p - host_start);
    if (!host_len || host_len >= host_size) {
        return false;
    }
    kmemcpy(host, host_start, host_len);
    host[host_len] = 0;
    *port = is_https ? 443 : 80;
    if (*p == ':') {
        p++;
        uint32_t value = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            value = value * 10u + (uint32_t)(*p - '0');
            if (value > 65535u) {
                return false;
            }
            p++;
            digits++;
        }
        if (!digits || value == 0) {
            return false;
        }
        *port = (uint16_t)value;
    }
    if (!*p) {
        kstrcpy(path, "/");
        return true;
    }
    if (*p != '/' && *p != '?' && *p != '#') {
        return false;
    }
    size_t path_len = 0;
    if (*p == '?') {
        if (path_len + 1 >= path_size) {
            return false;
        }
        path[path_len++] = '/';
    }
    while (*p && *p != '#' && path_len + 1 < path_size) {
        path[path_len++] = *p++;
    }
    path[path_len] = 0;
    if (!path[0]) {
        kstrcpy(path, "/");
    }
    return true;
}

static bool web_resolve_location(const char *base, const char *location,
                                 char *out, size_t out_size) {
    if (web_scheme(location, "http://") || web_scheme(location, "https://")) {
        if (kstrlen(location) >= out_size) {
            return false;
        }
        kstrcpy(out, location);
        return true;
    }
    char host[192];
    uint16_t port;
    bool secure;
    char base_path[WEB_URL_SIZE];
    if (!web_parse_url(base, host, sizeof(host), &port,
                       base_path, sizeof(base_path), &secure)) {
        return false;
    }
    if (location[0] == '/' && location[1] == '/') {
        int n = snprintf(out, out_size, "%s:%s",
                         secure ? "https" : "http", location);
        return n > 0 && (size_t)n < out_size;
    }
    if (location[0] == '/') {
        return web_make_url(secure, host, port, location, out, out_size);
    }

    char target[WEB_URL_SIZE];
    if (location[0] == '?') {
        size_t len = kstrlen(base_path);
        if (len + kstrlen(location) >= sizeof(target)) {
            return false;
        }
        kstrcpy(target, base_path);
        kstrcat(target, location);
    } else {
        size_t dir_len = 1;
        size_t base_len = kstrlen(base_path);
        for (size_t i = 0; i < base_len; i++) {
            if (base_path[i] == '/') {
                dir_len = i + 1;
            }
        }
        size_t loc_len = kstrlen(location);
        if (dir_len + loc_len >= sizeof(target)) {
            return false;
        }
        kmemcpy(target, base_path, dir_len);
        kmemcpy(target + dir_len, location, loc_len + 1);
    }
    return web_make_url(secure, host, port, target, out, out_size);
}

static void web_fetch_page(web_state_t *ws, const char *url) {
    if (!net_ready()) {
        set_error_page(ws, "No network adapter",
                       "Bordify could not find an Intel e1000 adapter. "
                       "Start QEMU with -nic user,model=e1000.");
        return;
    }

    char current[WEB_URL_SIZE];
    kstrncpy(current, url, sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;
    for (int redirect = 0; redirect < 5; redirect++) {
        char host[192];
        uint16_t port;
        bool secure;
        char path[WEB_URL_SIZE];
        if (!web_parse_url(current, host, sizeof(host), &port,
                           path, sizeof(path), &secure)) {
            set_error_page(ws, "Invalid web address",
                           "Use an address such as https://example.com/.");
            return;
        }
        char location[WEB_URL_SIZE];
        location[0] = 0;
        int status = 0;
        int bytes = secure
            ? net_https_get(host, port, path, ws->page, WEB_PAGE_SIZE,
                            location, sizeof(location), &status)
            : net_http_get(host, port, path, ws->page, WEB_PAGE_SIZE,
                           location, sizeof(location), &status);
        if (bytes < 0) {
            set_error_page(ws, "Could not load page", net_last_error());
            return;
        }
        if (status < 100 || status > 599) {
            set_error_page(ws, "Invalid HTTP response",
                           "The server did not return a valid HTTP response.");
            return;
        }
        if (status >= 300 && status < 400 && location[0]) {
            char next[WEB_URL_SIZE];
            if (!web_resolve_location(current, location, next, sizeof(next))) {
                set_error_page(ws, "Invalid redirect",
                               "The server redirected to an address Bordify cannot follow.");
                return;
            }
            kstrcpy(current, next);
            continue;
        }
        if (status >= 400) {
            char message[160];
            snprintf(message, sizeof(message), "The server returned HTTP %d.", status);
            set_error_page(ws, "Page failed to load", message);
            return;
        }
        ws->page[bytes] = 0;
        ws->page_length = bytes;
        ws->load_error = 0;
        kstrcpy(ws->url, current);
        return;
    }
    set_error_page(ws, "Too many redirects",
                   "The server redirected the request too many times.");
}

static void web_navigate(web_state_t *ws, const char *input, bool record_history) {
    char url[WEB_URL_SIZE];
    kstrncpy(url, input, sizeof(url) - 1);
    url[sizeof(url) - 1] = 0;
    ktrim(url);
    if (!url[0]) {
        kstrcpy(url, "bordify://home");
    }
    if (url[0] != '/' && !kstrstr(url, "://")) {
        char temp[WEB_URL_SIZE];
        if (kstrchr(url, '.') || kstrchr(url, ':')) {
            snprintf(temp, sizeof(temp), "https://%s", url);
        } else {
            snprintf(temp, sizeof(temp), "bordify://%s", url);
        }
        kstrcpy(url, temp);
    }

    ws->scroll = 0;
    ws->load_error = 0;
    kstrncpy(ws->url, url, sizeof(ws->url) - 1);
    ws->url[sizeof(ws->url) - 1] = 0;

    if (kstarts_with(url, "bordify://")) {
        const char *path = local_path_for_url(url);
        if (!path) {
            set_error_page(ws, "Page not found",
                           "That Bordify address does not point to an installed page.");
        } else {
            int bytes = fat16_read_path(path, ws->page, WEB_PAGE_SIZE - 1);
            if (bytes < 0) {
                kstrncpy(ws->page, home_fallback, WEB_PAGE_SIZE - 1);
                ws->page[WEB_PAGE_SIZE - 1] = 0;
                ws->page_length = (int)kstrlen(ws->page);
                ws->load_error = 1;
            } else {
                ws->page[bytes] = 0;
                ws->page_length = bytes;
            }
        }
    } else if (web_scheme(url, "http://") || web_scheme(url, "https://")) {
        web_fetch_page(ws, url);
    } else {
        set_error_page(ws, "Unsupported address",
                       "Use bordify://home, bordify://guide or an https:// address.");
    }

    if (record_history) {
        web_push_history(ws, url);
    }
}

static void draw_back_forward(web_state_t *ws, int x, int y) {
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    bool hover_back = ui_point_in(x, y, 30, 30, mouse.x, mouse.y);
    draw_icon_button(x, y, 0, hover_back);
    bool hover_forward = ui_point_in(x + 34, y, 30, 30, mouse.x, mouse.y);
    draw_icon_button(x + 34, y, 1, hover_forward);
    bool hover_reload = ui_point_in(x + 68, y, 30, 30, mouse.x, mouse.y);
    draw_icon_button(x + 68, y, 2, hover_reload);
    bool hover_home = ui_point_in(x + 102, y, 30, 30, mouse.x, mouse.y);
    draw_icon_button(x + 102, y, 3, hover_home);
    (void)ws;
}

static void web_draw(gui_window_t *window) {
    int x0 = gui_content_x(window);
    int y0 = gui_content_y(window);
    int w = gui_content_width(window);
    int h = gui_content_height(window);

    gfx_fill_rect(x0, y0, w, 52, settings_panel_alt());
    gfx_hline(x0, x0 + w - 1, y0 + 51, settings_line());
    draw_back_forward(&web, x0 + 13, y0 + 11);

    int go_w = 52;
    int field_x = x0 + 153;
    int field_w = w - (field_x - x0) - go_w - 26;
    mouse_state_t mouse;
    input_mouse_state(&mouse);
    bool field_hover = ui_point_in(field_x, y0 + 11, field_w, 30, mouse.x, mouse.y);
    if (field_hover && !web.editing) {
        gfx_rounded_fill(field_x, y0 + 11, field_w, 30, 7, settings_panel());
    }
    ui_field(field_x, y0 + 11, field_w, 30, web.editing ? web.edit : web.url,
             web.editing, false);
    int go_x = field_x + field_w + 8;
    bool go_hover = ui_point_in(go_x, y0 + 11, go_w, 30, mouse.x, mouse.y);
    ui_button(go_x, y0 + 11, go_w, 30, "Go", true, go_hover);

    int page_y = y0 + 52;
    int page_h = h - 52;
    gfx_fill_rect(x0, page_y, w, page_h, settings_panel());
    gfx_fill_rect(x0 + 1, page_y, w - 2, 4, settings_panel());

    gfx_clip_t saved = gfx_get_clip();
    gfx_set_clip(x0 + 1, page_y, w - 2, page_h);
    html_render(&web, web.page, x0 + 1, page_y, w - 2, page_h);
    int max_scroll = web.content_height - page_h + 20;
    if (max_scroll < 0) {
        max_scroll = 0;
    }
    if (web.scroll > max_scroll) {
        web.scroll = max_scroll;
    }
    if (web.scroll < 0) {
        web.scroll = 0;
    }
    if (max_scroll > 0) {
        int bar_x = x0 + w - 9;
        int bar_h = page_h - 24;
        int thumb_h = MAX(28, bar_h * page_h / web.content_height);
        int thumb_y = page_y + 12 + (bar_h - thumb_h) * web.scroll / max_scroll;
        gfx_rounded_fill(bar_x, page_y + 12, 4, bar_h, 2, settings_panel_alt());
        gfx_rounded_fill(bar_x, thumb_y, 4, thumb_h, 2, settings_line());
    }
    web.page_height = page_h;
    gfx_set_clip_rect(saved);
}

static void web_click_history(web_state_t *ws, int direction) {
    int target = ws->history_index + direction;
    if (target < 0 || target >= ws->history_count) {
        return;
    }
    ws->history_index = target;
    web_navigate(ws, ws->history[target], false);
    ws->history_index = target;
}

static void web_mouse_down(gui_window_t *window, int x, int y) {
    int x0 = gui_content_x(window);
    int y0 = gui_content_y(window);
    int w = gui_content_width(window);
    int h = gui_content_height(window);
    UNUSED(h);
    int go_w = 52;
    int field_x = x0 + 153;
    int field_w = w - (field_x - x0) - go_w - 26;
    int go_x = field_x + field_w + 8;

    if (ui_point_in(x0 + 13, y0 + 11, 30, 30, x, y)) {
        web_click_history(&web, -1);
        return;
    }
    if (ui_point_in(x0 + 47, y0 + 11, 30, 30, x, y)) {
        web_click_history(&web, 1);
        return;
    }
    if (ui_point_in(x0 + 81, y0 + 11, 30, 30, x, y)) {
        web_navigate(&web, web.url, false);
        return;
    }
    if (ui_point_in(x0 + 115, y0 + 11, 30, 30, x, y)) {
        web_navigate(&web, "bordify://home", true);
        return;
    }
    if (ui_point_in(field_x, y0 + 11, field_w, 30, x, y)) {
        web.editing = true;
        web.edit[0] = 0;
        return;
    }
    if (ui_point_in(go_x, y0 + 11, go_w, 30, x, y)) {
        web_navigate(&web, web.editing ? web.edit : web.url, true);
        web.editing = false;
        return;
    }
    if (y < y0 + 52) {
        return;
    }
    if (web.editing) {
        web.editing = false;
        return;
    }
    for (int i = web.link_count - 1; i >= 0; i--) {
        web_link_t *link = &web.links[i];
        if (ui_point_in(link->x, link->y, link->w, link->h, x, y)) {
            web_navigate(&web, link->href, true);
            return;
        }
    }
}

static void web_key(gui_window_t *window, const key_event_t *event) {
    UNUSED(window);
    if (web.editing) {
        if (event->ch >= 32 && event->ch < 127) {
            size_t len = kstrlen(web.edit);
            if (len + 1 < sizeof(web.edit)) {
                web.edit[len] = event->ch;
                web.edit[len + 1] = 0;
            }
        } else if (event->code == KEY_BACKSPACE) {
            size_t len = kstrlen(web.edit);
            if (len) {
                web.edit[len - 1] = 0;
            }
        } else if (event->code == KEY_ENTER) {
            web_navigate(&web, web.edit, true);
            web.editing = false;
        } else if (event->code == KEY_ESC) {
            web.editing = false;
        }
        return;
    }
    if (event->code == KEY_TAB || event->ch == '/') {
        web.editing = true;
        web.edit[0] = 0;
        return;
    }
    if (event->code == KEY_DOWN) {
        web.scroll += 36;
    } else if (event->code == KEY_UP) {
        web.scroll -= 36;
        if (web.scroll < 0) {
            web.scroll = 0;
        }
    } else if (event->code == KEY_HOME) {
        web_navigate(&web, "bordify://home", true);
    }
}

static void web_init(gui_window_t *window) {
    UNUSED(window);
    kmemset(&web, 0, sizeof(web));
    kstrcpy(web.status, "ready");
    web_navigate(&web, "bordify://home", true);
}

const gui_app_t web_app = {
    .title = "web",
    .width = 860,
    .height = 600,
    .init = web_init,
    .draw = web_draw,
    .mouse_down = web_mouse_down,
    .mouse_up = NULLPTR,
    .key = web_key,
    .tick = NULLPTR,
};
