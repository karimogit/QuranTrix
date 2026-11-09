#include "home_article.h"
#include "home_types.h"
#include <stdio.h>
#include <string.h>

/* 12 px Atkinson, about 40 columns across the 372 px text column, 10 rows above the footer. */
#define COLS 40
#define ROWS 10

static char story_url[HOME_FEED_URL_BYTES];
static char story[HOME_ARTICLE_BYTES];
static int story_page;
static bool story_known, story_cut;

static int utf8_span(const char *s)
{
    unsigned char c = (unsigned char)*s;
    if (c < 0x80 || !c)
        return 1;
    if ((c & 0xe0) == 0xc0)
        return 2;
    if ((c & 0xf0) == 0xe0)
        return 3;
    if ((c & 0xf8) == 0xf0)
        return 4;
    return 1;
}

/* Walk one visual line. Returns the start of the next line. */
static const char *next_line(const char *p)
{
    const char *start = p, *brk = p;
    int col = 0;
    while (*p && *p != '\n' && col < COLS) {
        if (*p == ' ')
            brk = p + 1;
        int n = utf8_span(p);
        p += n;
        col++;
    }
    if (*p == '\n')
        return p + 1;
    if (*p && brk > start)
        return brk;
    return p;
}

static int line_count(void)
{
    if (!story[0])
        return 0;
    int lines = 0;
    const char *p = story;
    while (*p && lines < 400) {
        const char *n = next_line(p);
        if (n == p)
            break;
        p = n;
        lines++;
    }
    return lines;
}

void home_article_set(const char *url, const char *plain, bool truncated)
{
    if (!url)
        url = "";
    if (!plain)
        plain = "";
    bool same = story_known && !strcmp(story_url, url);
    snprintf(story_url, sizeof story_url, "%s", url);
    snprintf(story, sizeof story, "%s", plain);
    story_known = true;
    story_cut = truncated;
    if (!same)
        story_page = 0;
}

bool home_article_matches(const char *url)
{
    return story_known && url && !strcmp(story_url, url);
}

int home_article_pages(const char *url)
{
    if (!home_article_matches(url) || !story[0])
        return 0;
    int lines = line_count();
    if (lines < 1)
        return 0;
    return (lines + ROWS - 1) / ROWS;
}

int home_article_page(const char *url)
{
    int pages = home_article_pages(url);
    if (pages < 1)
        return 0;
    if (story_page >= pages)
        story_page = pages - 1;
    return story_page;
}

bool home_article_turn(const char *url, int delta)
{
    int pages = home_article_pages(url);
    if (pages < 2)
        return false;
    int next = home_article_page(url) + delta;
    if (next < 0 || next >= pages)
        return false;
    story_page = next;
    return true;
}

bool home_article_truncated(void)
{
    return story_cut;
}

void home_article_page_text(const char *url, char *out, size_t cap)
{
    if (!out || cap == 0)
        return;
    out[0] = 0;
    if (!home_article_matches(url) || !story[0])
        return;
    const char *p = story;
    int skip = home_article_page(url) * ROWS;
    for (int i = 0; i < skip && *p; i++)
        p = next_line(p);
    size_t used = 0;
    for (int i = 0; i < ROWS && *p && used + 1 < cap; i++) {
        const char *n = next_line(p);
        size_t len = (size_t)(n - p);
        if (len && n[-1] == '\n')
            len--;
        while (len && p[len - 1] == ' ')
            len--;
        if (used && used + 1 < cap)
            out[used++] = '\n';
        if (len > cap - used - 1)
            len = cap - used - 1;
        memcpy(out + used, p, len);
        used += len;
        out[used] = 0;
        p = n;
    }
}
