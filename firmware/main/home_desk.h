#ifndef HOME_DESK_H
#define HOME_DESK_H
#include <stdbool.h>
#include <time.h>
/* One-view desk: the NFC field marks the current prayer, the round button plays the radio. */
void home_desk_init(void);
void home_desk_listen(void);
/* Sample the NFC field and the local date. 2 = the day rolled over and cleared the marks. */
int home_desk_poll(void);
/* Bit i is set once prayer i has been tapped off today. */
int home_desk_prayers(void);
/* A phone is on the tag, or just was. The round button must not start or stop the music. */
bool home_desk_button_blocked(void);
bool home_desk_radio(void);
void home_desk_radio_toggle(void);
/* Midnight through 05:59 local: no automatic paper refresh. */
bool home_desk_night_quiet(const struct tm *local);
/* Enough time passed since the last full refresh (four hours). */
bool home_desk_periodic_due(int64_t mono);
void home_desk_painted(int64_t mono);
#endif
