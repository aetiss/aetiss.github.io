/*
 * blog.c — C/WASM blog engine
 *
 * Posts are stored as C structs. A minimal markdown parser converts them to
 * HTML. EM_JS macros bridge into the DOM. Hash routing handled in C.
 */

#include <emscripten.h>
#include <emscripten/em_js.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ─── Data types ──────────────────────────────────────────────── */

typedef struct {
    const char *slug;
    const char *title;
    const char *date;
    const char *excerpt;
    const char *content;
} BlogPost;

/* ─── Post data ───────────────────────────────────────────────── */

static const BlogPost posts[] = {
#include "posts/why-c-wasm.h"
#include "posts/wasm-games.h"
};

static const int NUM_POSTS = (int)(sizeof(posts) / sizeof(posts[0]));

/* ─── EM_JS — DOM bridges ─────────────────────────────────────── */

EM_JS(void, js_set_inner_html, (const char *sel, const char *html), {
    var el = document.querySelector(UTF8ToString(sel));
    if (el) el.innerHTML = UTF8ToString(html);
});

EM_JS(void, js_set_title, (const char *t), {
    document.title = UTF8ToString(t) + ' \u2014 aetiss';
});

EM_JS(void, js_scroll_top, (void), {
    window.scrollTo(0, 0);
});

EM_JS(int, js_get_hash_len, (void), {
    return lengthBytesUTF8(window.location.hash.slice(1)) + 1;
});

EM_JS(void, js_get_hash, (char *buf, int sz), {
    stringToUTF8(window.location.hash.slice(1), buf, sz);
});

EM_JS(void, js_register_hashchange, (void), {
    window.addEventListener('hashchange', function() {
        Module._blog_route();
    });
});

/* ─── Minimal Markdown → HTML parser ─────────────────────────── */
/*
 * Supported:
 *   ## heading      → <h2>
 *   **text**        → <strong>
 *   `code`          → <code>
 *   ```\nblock\n``` → <pre><code>
 *   \n\n            → paragraph break
 *   plain text      → <p>
 */

#define OUT_MAX (1 << 17)   /* 128 KB output buffer */

static void html_escape(const char *s, int len, char *out, int *oi) {
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if      (c == '<')  { memcpy(out + *oi, "&lt;",  4); *oi += 4; }
        else if (c == '>')  { memcpy(out + *oi, "&gt;",  4); *oi += 4; }
        else if (c == '&')  { memcpy(out + *oi, "&amp;", 5); *oi += 5; }
        else if (c == '"')  { memcpy(out + *oi, "&quot;",6); *oi += 6; }
        else                { out[(*oi)++] = c; }
    }
}

/* emit inline markup: **bold**, `code` */
static void emit_inline(const char *s, int len, char *out, int *oi) {
    int i = 0;
    while (i < len) {
        if (i + 1 < len && s[i] == '*' && s[i+1] == '*') {
            /* find closing ** */
            int j = i + 2;
            while (j + 1 < len && !(s[j] == '*' && s[j+1] == '*')) j++;
            if (j + 1 < len) {
                const char *open  = "<strong>";
                const char *close = "</strong>";
                memcpy(out + *oi, open, 8);  *oi += 8;
                emit_inline(s + i + 2, j - i - 2, out, oi);
                memcpy(out + *oi, close, 9); *oi += 9;
                i = j + 2;
                continue;
            }
        }
        if (s[i] == '`' && (i == 0 || s[i-1] != '`')) {
            int j = i + 1;
            while (j < len && s[j] != '`') j++;
            if (j < len) {
                const char *open  = "<code>";
                const char *close = "</code>";
                memcpy(out + *oi, open,  6); *oi += 6;
                html_escape(s + i + 1, j - i - 1, out, oi);
                memcpy(out + *oi, close, 7); *oi += 7;
                i = j + 1;
                continue;
            }
        }
        html_escape(s + i, 1, out, oi);
        i++;
    }
}

static char *parse_markdown(const char *src) {
    char *out = (char *)malloc(OUT_MAX);
    if (!out) return NULL;
    int oi = 0;

    int n = (int)strlen(src);
    int i = 0;
    int in_p = 0;
    int in_pre = 0;

    while (i < n && oi < OUT_MAX - 512) {
        /* code block ``` */
        if (!in_pre && i + 2 < n && src[i]=='`' && src[i+1]=='`' && src[i+2]=='`') {
            if (in_p) { memcpy(out+oi,"</p>",4); oi+=4; in_p=0; }
            i += 3;
            while (i < n && src[i] == '\n') i++;   /* skip first newline */
            const char *open = "<pre><code>";
            memcpy(out+oi, open, 11); oi += 11;
            in_pre = 1;
            continue;
        }
        if (in_pre && i + 2 < n && src[i]=='`' && src[i+1]=='`' && src[i+2]=='`') {
            const char *close = "</code></pre>\n";
            memcpy(out+oi, close, 14); oi += 14;
            i += 3;
            in_pre = 0;
            continue;
        }
        if (in_pre) {
            html_escape(src+i, 1, out, &oi);
            i++;
            continue;
        }

        /* heading ## */
        if (src[i] == '#') {
            if (in_p) { memcpy(out+oi,"</p>",4); oi+=4; in_p=0; }
            int level = 0;
            while (i < n && src[i] == '#') { level++; i++; }
            while (i < n && src[i] == ' ') i++;
            int start = i;
            while (i < n && src[i] != '\n') i++;
            char tag[8];
            int tl = snprintf(tag, sizeof(tag), "<h%d>", level < 4 ? level + 1 : 4);
            memcpy(out+oi, tag, tl); oi += tl;
            emit_inline(src + start, i - start, out, &oi);
            snprintf(tag, sizeof(tag), "</h%d>\n", level < 4 ? level + 1 : 4);
            int cl = (int)strlen(tag);
            memcpy(out+oi, tag, cl); oi += cl;
            continue;
        }

        /* blank line → paragraph break */
        if (src[i] == '\n') {
            int j = i;
            while (j < n && src[j] == '\n') j++;
            if (j - i >= 2) {
                if (in_p) { memcpy(out+oi,"</p>",4); oi+=4; in_p=0; }
                i = j;
                continue;
            }
            /* single newline in a paragraph becomes space */
            if (in_p) { out[oi++] = ' '; }
            i++;
            continue;
        }

        /* start a paragraph */
        if (!in_p) {
            memcpy(out+oi,"<p>",3); oi+=3; in_p=1;
        }
        emit_inline(src+i, 1, out, &oi);
        i++;
    }

    if (in_p)  { memcpy(out+oi,"</p>",4); oi+=4; }
    if (in_pre){ memcpy(out+oi,"</code></pre>",13); oi+=13; }
    out[oi] = '\0';
    return out;
}

/* ─── Rendering ───────────────────────────────────────────────── */

static void render_list(void) {
    char *html = (char *)malloc(OUT_MAX);
    if (!html) return;
    int oi = 0;

    oi += snprintf(html+oi, OUT_MAX-oi,
        "<h1 class=\"blog-title\">Blog</h1>"
        "<p class=\"blog-subtitle\">Writing about C, WebAssembly, and building things from scratch.</p>"
    );

    for (int i = 0; i < NUM_POSTS; i++) {
        oi += snprintf(html+oi, OUT_MAX-oi,
            "<article class=\"post-preview\">"
              "<h2><a href=\"blog.html#%s\">%s</a></h2>"
              "<p class=\"post-meta\">%s</p>"
              "<p class=\"post-excerpt\">%s</p>"
            "</article>",
            posts[i].slug, posts[i].title,
            posts[i].date,
            posts[i].excerpt
        );
    }

    js_set_inner_html("#blog-content", html);
    js_set_title("Blog");
    js_scroll_top();
    free(html);
}

static void render_post(const char *slug) {
    for (int i = 0; i < NUM_POSTS; i++) {
        if (strcmp(posts[i].slug, slug) == 0) {
            char *body = parse_markdown(posts[i].content);
            if (!body) return;

            char *html = (char *)malloc(OUT_MAX);
            if (!html) { free(body); return; }

            snprintf(html, OUT_MAX,
                "<article class=\"post-full\">"
                  "<a href=\"blog.html\" class=\"back-link\">&#8592; All posts</a>"
                  "<h1>%s</h1>"
                  "<p class=\"post-meta\">%s</p>"
                  "<div class=\"post-body\">%s</div>"
                "</article>",
                posts[i].title, posts[i].date, body
            );

            js_set_inner_html("#blog-content", html);
            js_set_title(posts[i].title);
            js_scroll_top();
            free(body);
            free(html);
            return;
        }
    }
    js_set_inner_html("#blog-content",
        "<article class=\"post-full\">"
        "<a href=\"blog.html\" class=\"back-link\">&#8592; All posts</a>"
        "<p>Post not found.</p>"
        "</article>"
    );
}

/* ─── Router — exported so JS hashchange can call it ─────────── */

EMSCRIPTEN_KEEPALIVE
void blog_route(void) {
    int len = js_get_hash_len();
    if (len > 1) {
        char *slug = (char *)malloc(len + 1);
        if (!slug) return;
        js_get_hash(slug, len + 1);
        if (slug[0] != '\0') {
            render_post(slug);
        } else {
            render_list();
        }
        free(slug);
    } else {
        render_list();
    }
}

/* ─── Entry point ─────────────────────────────────────────────── */

int main(void) {
    js_register_hashchange();
    blog_route();
    return 0;
}
