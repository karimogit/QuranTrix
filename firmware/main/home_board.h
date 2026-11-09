#ifndef HOME_BOARD_H
#define HOME_BOARD_H
#include <stdbool.h>
#include <stdint.h>

/* 0 = auto (defaults to NOTE4C colour), 1 = NOTE4C, 2 = NOTE4 monochrome */
#define HOME_PANEL_AUTO 0
#define HOME_PANEL_NOTE4C 1
#define HOME_PANEL_NOTE4 2

bool home_board_mono(void);
const char *home_board_model_name(void);
int home_board_render_intensity(int cfg_intensity);

#endif
