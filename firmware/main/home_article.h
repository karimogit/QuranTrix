#ifndef HOME_ARTICLE_H
#define HOME_ARTICLE_H
#include <stdbool.h>
#include <stddef.h>
/* Plain article text lives in RAM only. It is not part of home_feed_t: that
 * struct is stored in the 8 KiB NVS cache, and a story body does not fit. */
#define HOME_ARTICLE_BYTES 4500
void home_article_set(const char *url, const char *plain, bool truncated);
/* True once a fetch has stored a result for this story, even if the body was empty. */
bool home_article_matches(const char *url);
bool home_article_turn(const char *url, int delta);
int home_article_page(const char *url);
int home_article_pages(const char *url);
bool home_article_truncated(void);
void home_article_page_text(const char *url, char *out, size_t cap);
#endif
