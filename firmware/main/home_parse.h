#ifndef HOME_PARSE_H
#define HOME_PARSE_H
#include "home_types.h"

/* Parsers accept at most 128 KiB, never fetch resources, and only assign *out
 * on success. Caller owns HTTP freshness/ETag/checked/fetched/expires fields.
 * Weather uses forecast instants at/after now, no more than 1 h ahead; optional
 * missing values are NAN. low/high are sampled extrema of 24 hourly forecast
 * instants starting at forecast_at (NAN unless all 24 samples are available).
 * Feed selects the newest valid dated entry (at most 5 min in the future),
 * falling back to the first valid undated entry if no dated item is available.
 * Undated entries have published_at=issued_at=0, never a fabricated timestamp.
 * HTTPS absolute links only; absent/relative/unsafe links produce an empty URL.
 * Returns Unix UTC seconds, or -1 for malformed/unsupported dates. */
int64_t home_parse_time(const char *text);
bool home_parse_weather(const char *json, size_t len, home_weather_t *out,
                        int64_t now, char error[97]);
bool home_parse_feed(const char *xml, size_t len, home_feed_t *out,
                     int64_t now, char error[97]);
/* Preserve editorial order: first valid item; still validate the entire XML. */
bool home_parse_feed_first(const char *xml, size_t len, home_feed_t *out,
                           int64_t now, char error[97]);
/* Plain text of the story chosen by the last successful feed parse. Empty when
 * that entry had no description. Valid until the next parse_feed call. */
const char *home_parse_last_article(void);
bool home_parse_article_truncated(void);
/* Secret iCal (Google Calendar "Secret address in iCal format"). Keeps the
 * soonest few events from today through the next 45 days. */
bool home_parse_calendar(const char *ics, size_t len, home_calendar_t *out,
                         int64_t now, const char *zone, char error[97]);
#endif
