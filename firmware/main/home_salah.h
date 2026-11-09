#ifndef HOME_SALAH_H
#define HOME_SALAH_H
#include "home_types.h"
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

enum { SALAH_FAJR = 0, SALAH_DHUHR = 1, SALAH_ASR = 2, SALAH_MAGHRIB = 3, SALAH_ISHA = 4,
       SALAH_COUNT = 5 };

/* Prayer times are local minutes from midnight. The Hijri date, Ramadan count and the next
 * fixed holiday do not need a network. An agenda, when one has been fetched, replaces the
 * holiday and the Ramadan boundary. */
typedef struct {
    bool valid;
    int minute[SALAH_COUNT];
    int sunrise; /* local minutes, sun at 0.833 deg */
    int sunset;
    int current; /* 0..4, the prayer a tap marks. -1 until the clock and place are known. */
    int hy, hm, hd;
    bool in_ramadan;
    int ramadan_days;
    char holiday[48];
    int holiday_month; /* 1..12, 0 when none */
    int holiday_day;
    int holiday_days; /* 0 = today, -1 = none */
} home_salah_day_t;

bool home_salah_compute(double lat, double lon, int32_t tz_offset_sec, const struct tm *local,
                        int method, int madhab, bool located, home_salah_day_t *out);
void home_salah_apply_agenda(home_salah_day_t *day, const struct tm *local,
                             const home_calendar_t *cal, const char *timezone);
typedef struct {
    const char *words;
    const char *meaning;
} home_dua_t;

const char *home_salah_name(int index);
const char *home_hijri_month(int month);
void home_hijri_from_tm(const struct tm *tm, int *hy, int *hm, int *hd);
const home_dua_t *home_dua_for_yday(int yday);
#endif
