#include "home_salah.h"
#include "home_duas.h"
#include "home_places.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const char *home_salah_name(int index)
{
    static const char *names[] = {"Fajr", "Dhuhr", "Asr", "Maghrib", "Isha"};
    if (index < 0 || index >= SALAH_COUNT)
        return "";
    return names[index];
}

const home_dua_t *home_dua_for_yday(int yday)
{
    if (yday < 0)
        yday = 0;
    if (home_duas_count < 1)
        return &home_duas[0];
    return &home_duas[yday % home_duas_count];
}

const char *home_hijri_month(int month)
{
    static const char *names[] = {"Muharram", "Safar",     "Rabi I",    "Rabi II",
                                  "Jumada I", "Jumada II", "Rajab",      "Shaban",
                                  "Ramadan",  "Shawwal",   "Dhul Qidah", "Dhul Hijjah"};
    if (month < 1 || month > 12)
        return "";
    return names[month - 1];
}

static int civil_jd(int y, int m, int d)
{
    int a = (14 - m) / 12;
    y = y + 4800 - a;
    m = m + 12 * a - 3;
    return d + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100 + y / 400 - 32045;
}

static void jd_gregorian(int jd, int *y, int *m, int *d)
{
    int a = jd + 32044;
    int b = (4 * a + 3) / 146097;
    int c = a - (146097 * b) / 4;
    int e = (4 * c + 3) / 1461;
    int f = c - (1461 * e) / 4;
    int g = (5 * f + 2) / 153;
    *d = f - (153 * g + 2) / 5 + 1;
    *m = g + 3 - 12 * (g / 10);
    *y = 100 * b + e - 4800 + g / 10;
}

static void jd_hijri(int jd, int *hy, int *hm, int *hd)
{
    int l = jd - 1948440 + 10632;
    int n = (l - 1) / 10631;
    int j;
    l = l - 10631 * n + 354;
    j = ((10985 - l) / 5316) * ((50 * l) / 17719) + (l / 5670) * ((43 * l) / 15238);
    l = l - ((30 - j) / 15) * ((17719 * j) / 50) - (j / 16) * ((15238 * j) / 43) + 29;
    *hm = (24 * l) / 709;
    *hd = l - (709 * *hm) / 24;
    *hy = 30 * n + j - 30;
}

void home_hijri_from_tm(const struct tm *tm, int *hy, int *hm, int *hd)
{
    if (!tm || !hy || !hm || !hd)
        return;
    jd_hijri(civil_jd(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday), hy, hm, hd);
}

static int hijri_jd(int hy, int hm, int hd)
{
    return (11 * hy + 3) / 30 + 354 * hy + 30 * hm - (hm - 1) / 2 + hd + 1948440 - 385;
}

static double wrap360(double a)
{
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

static double wrap24(double a)
{
    a = fmod(a, 24.0);
    return a < 0 ? a + 24.0 : a;
}

static int minute_of(double hours)
{
    int m;
    if (!isfinite(hours))
        return -1;
    hours = wrap24(hours);
    m = (int)lround(hours * 60.0);
    if (m >= 24 * 60)
        m -= 24 * 60;
    if (m < 0)
        m += 24 * 60;
    return m;
}

static int wrap_minute(int minute)
{
    minute %= 24 * 60;
    if (minute < 0)
        minute += 24 * 60;
    return minute;
}

static double julian(int y, int m, int d)
{
    int a, b;
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    a = y / 100;
    b = 2 - a + a / 4;
    return floor(365.25 * (y + 4716)) + floor(30.6001 * (m + 1)) + d + b - 1524.5;
}

static void sun_position(double jd, double *decl, double *eqt)
{
    double day = jd - 2451545.0;
    double g = wrap360(357.529 + 0.98560028 * day);
    double q = wrap360(280.459 + 0.98564736 * day);
    double L = wrap360(q + 1.915 * sin(g * M_PI / 180.0) + 0.020 * sin(2 * g * M_PI / 180.0));
    double e = 23.439 - 0.00000036 * day;
    double ra = atan2(cos(e * M_PI / 180.0) * sin(L * M_PI / 180.0), cos(L * M_PI / 180.0)) *
                180.0 / M_PI;
    ra = wrap360(ra) / 15.0;
    *eqt = q / 15.0 - ra;
    if (*eqt > 12)
        *eqt -= 24;
    if (*eqt < -12)
        *eqt += 24;
    *decl = asin(sin(e * M_PI / 180.0) * sin(L * M_PI / 180.0)) * 180.0 / M_PI;
}

static double hour_angle(double angle, double lat, double decl)
{
    double num = -sin(angle * M_PI / 180.0) - sin(lat * M_PI / 180.0) * sin(decl * M_PI / 180.0);
    double den = cos(lat * M_PI / 180.0) * cos(decl * M_PI / 180.0);
    double cos_h;
    if (fabs(den) < 1e-9)
        return NAN;
    cos_h = num / den;
    if (cos_h < -1 || cos_h > 1)
        return NAN;
    return acos(cos_h) * 180.0 / M_PI / 15.0;
}

static void fill_calendar(home_salah_day_t *out, const struct tm *local)
{
    static const struct {
        int month, day;
        const char *name;
    } holidays[] = {
        {1, 1, "Islamic New Year"}, {1, 10, "Ashura"},           {3, 12, "Mawlid"},
        {7, 27, "Isra and Mi'raj"}, {8, 15, "Mid-Shaban"},       {9, 27, "Laylat al-Qadr"},
        {10, 1, "Eid al-Fitr"},     {12, 9, "Day of Arafah"},    {12, 10, "Eid al-Adha"},
    };
    int y = local->tm_year + 1900;
    int m = local->tm_mon + 1;
    int d = local->tm_mday;
    int today = civil_jd(y, m, d);
    int best = 100000;
    int best_jd = 0;
    const char *best_name = NULL;
    int i;
    jd_hijri(today, &out->hy, &out->hm, &out->hd);
    if (out->hm == 9) {
        out->in_ramadan = true;
        out->ramadan_days = hijri_jd(out->hy, 10, 1) - today;
    } else if (out->hm < 9) {
        out->ramadan_days = hijri_jd(out->hy, 9, 1) - today;
    } else {
        out->ramadan_days = hijri_jd(out->hy + 1, 9, 1) - today;
    }
    if (out->ramadan_days < 1)
        out->ramadan_days = 1;
    for (int year = 0; year < 2; year++) {
        for (i = 0; i < (int)(sizeof holidays / sizeof holidays[0]); i++) {
            int jd = hijri_jd(out->hy + year, holidays[i].month, holidays[i].day);
            int delta = jd - today;
            if (delta >= 0 && delta < best) {
                best = delta;
                best_jd = jd;
                best_name = holidays[i].name;
            }
        }
    }
    if (best_name) {
        int gy, gm, gd;
        jd_gregorian(best_jd, &gy, &gm, &gd);
        snprintf(out->holiday, sizeof out->holiday, "%s", best_name);
        out->holiday_month = gm;
        out->holiday_day = gd;
        out->holiday_days = best;
    }
}

bool home_salah_compute(double lat, double lon, int32_t tz_offset_sec, const struct tm *local,
                        int method, int madhab, bool located, home_salah_day_t *out)
{
    static const struct {
        double fajr, isha;
        int isha_after;
    } methods[] = {
        {18.0, 17.0, 0}, {15.0, 15.0, 0}, {19.5, 17.5, 0}, {18.5, 0.0, 90}, {18.0, 18.0, 0},
    };
    double decl, eqt, noon, tz, span;
    int i, now_m;
    if (!local || !out)
        return false;
    memset(out, 0, sizeof *out);
    out->current = -1;
    out->holiday_days = -1;
    for (i = 0; i < SALAH_COUNT; i++)
        out->minute[i] = -1;
    if (method < 0 || method > 4)
        method = 0;
    if (madhab < 0 || madhab > 1)
        madhab = 0;
    fill_calendar(out, local);
    if (!located || lat < -90 || lat > 90 || lon < -180 || lon > 180)
        return true;
    sun_position(julian(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday) - lon / 360.0,
                 &decl, &eqt);
    tz = (double)tz_offset_sec / 3600.0;
    noon = wrap24(12.0 - eqt + tz - lon / 15.0);
    out->minute[SALAH_DHUHR] = minute_of(noon);
    span = hour_angle(0.833, lat, decl);
    out->sunrise = -1;
    out->sunset = -1;
    if (isfinite(span)) {
        out->sunrise = minute_of(noon - span);
        out->sunset = minute_of(noon + span);
        out->minute[SALAH_MAGHRIB] = out->sunset;
        out->minute[SALAH_FAJR] = minute_of(noon - hour_angle(methods[method].fajr, lat, decl));
        if (out->minute[SALAH_FAJR] < 0)
            out->minute[SALAH_FAJR] = wrap_minute(minute_of(noon - span) - 90);
        if (methods[method].isha_after > 0)
            out->minute[SALAH_ISHA] =
                wrap_minute(out->minute[SALAH_MAGHRIB] + methods[method].isha_after);
        else {
            out->minute[SALAH_ISHA] = minute_of(noon + hour_angle(methods[method].isha, lat, decl));
            if (out->minute[SALAH_ISHA] < 0)
                out->minute[SALAH_ISHA] = wrap_minute(out->minute[SALAH_MAGHRIB] + 90);
        }
    }
    {
        double factor = madhab ? 2.0 : 1.0;
        double delta = factor + tan((lat - decl) * M_PI / 180.0);
        double angle = -atan(1.0 / delta) * 180.0 / M_PI;
        double asr = hour_angle(angle, lat, decl);
        if (isfinite(asr))
            out->minute[SALAH_ASR] = minute_of(noon + asr);
    }
    out->valid = out->minute[SALAH_DHUHR] >= 0 && out->minute[SALAH_MAGHRIB] >= 0 &&
                 out->minute[SALAH_FAJR] >= 0 && out->minute[SALAH_ISHA] >= 0;
    if (!out->valid)
        return true;
    now_m = local->tm_hour * 60 + local->tm_min;
    out->current = SALAH_FAJR;
    for (i = 0; i < SALAH_COUNT; i++)
        if (out->minute[i] >= 0 && now_m >= out->minute[i])
            out->current = i;
    return true;
}

static bool contains_ci(const char *hay, const char *needle)
{
    size_t n;
    if (!hay || !needle)
        return false;
    n = strlen(needle);
    for (size_t i = 0; hay[i]; i++) {
        size_t k = 0;
        while (k < n && hay[i + k] &&
               tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k]))
            k++;
        if (k == n)
            return true;
    }
    return false;
}

static void clean_title(char *dst, size_t len, const char *src)
{
    const char *cut;
    size_t n;
    if (!dst || !len)
        return;
    if (!src)
        src = "";
    cut = strstr(src, " (");
    n = cut ? (size_t)(cut - src) : strlen(src);
    if (n >= len)
        n = len - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

void home_salah_apply_agenda(home_salah_day_t *day, const struct tm *local,
                             const home_calendar_t *cal, const char *timezone)
{
    int today, soonest = -1, soon_i = -1, ramadan = -1, eid = -1;
    if (!day || !local || !cal || !timezone || !timezone[0] || !cal->count)
        return;
    today = civil_jd(local->tm_year + 1900, local->tm_mon + 1, local->tm_mday);
    for (int i = 0; i < cal->count && i < HOME_CAL_ITEMS; i++) {
        struct tm when;
        int days;
        if (cal->item[i].start <= 0 || !cal->item[i].title[0])
            continue;
        if (!home_tz_localtime(timezone, cal->item[i].start, &when))
            continue;
        days = civil_jd(when.tm_year + 1900, when.tm_mon + 1, when.tm_mday) - today;
        if (days < 0)
            continue;
        if (soonest < 0 || days < soonest) {
            soonest = days;
            soon_i = i;
        }
        if (contains_ci(cal->item[i].title, "ramadan") && !contains_ci(cal->item[i].title, "eid")) {
            if (ramadan < 0 || days < ramadan)
                ramadan = days;
        }
        if (contains_ci(cal->item[i].title, "fitr")) {
            if (eid < 0 || days < eid)
                eid = days;
        }
    }
    if (ramadan > 0) {
        day->in_ramadan = false;
        day->ramadan_days = ramadan;
    } else if (ramadan == 0) {
        day->in_ramadan = true;
        day->ramadan_days = eid > 0 ? eid : 30;
    } else if (eid > 0 && eid <= 30) {
        day->in_ramadan = true;
        day->ramadan_days = eid;
    }
    if (soon_i >= 0) {
        struct tm when;
        clean_title(day->holiday, sizeof day->holiday, cal->item[soon_i].title);
        if (home_tz_localtime(timezone, cal->item[soon_i].start, &when)) {
            day->holiday_month = when.tm_mon + 1;
            day->holiday_day = when.tm_mday;
        }
        day->holiday_days = soonest;
    }
}

#ifdef HOME_SALAH_TEST
int main(void)
{
    struct tm tm = {.tm_year = 126, .tm_mon = 10, .tm_mday = 9, .tm_hour = 13, .tm_min = 0};
    home_salah_day_t day;
    home_salah_compute(52.3676, 4.9041, 3600, &tm, 0, 0, true, &day);
    printf("valid %d fajr %d dhuhr %d asr %d maghrib %d isha %d current %d\n", day.valid,
           day.minute[0], day.minute[1], day.minute[2], day.minute[3], day.minute[4], day.current);
    printf("hijri %d-%d-%d in %d days %d\n", day.hy, day.hm, day.hd, day.in_ramadan,
           day.ramadan_days);
    printf("holiday %s in %d days on %d/%d\n", day.holiday, day.holiday_days, day.holiday_day,
           day.holiday_month);
    return day.valid && day.minute[0] == 5 * 60 + 53 && day.minute[1] == 12 * 60 + 24 &&
                   day.ramadan_days == 91 && !day.in_ramadan
               ? 0
               : 1;
}
#endif
