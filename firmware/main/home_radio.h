#ifndef HOME_RADIO_H
#define HOME_RADIO_H
#include <stdbool.h>
/* Live Quran streams. The addresses are stored with the desk settings. */
void home_radio_toggle(void);
bool home_radio_on(void);
/* Hold the upper side button: next station; hold the lower side button: previous. */
void home_radio_station_step(int delta);
/* Short label for the stream (e.g. Quran, Qurango). The dashboard always reads Quran. */
const char *home_radio_station_label(void);
/* Side buttons. Heard immediately; the paper is not redrawn. */
void home_radio_volume_step(int delta);
/* A short tick. While the radio is playing it is mixed in; otherwise the speaker plays it alone. */
void home_radio_click(void);
/* The call to prayer, once. Skipped while the radio, a chime, or the speaker is busy.
 * An NFC tap during the call mixes through the same path as a radio tap. */
void home_radio_adhan(void);
#endif
