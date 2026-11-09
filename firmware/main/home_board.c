#include "home_board.h"
#include "qurantrix_settings.h"

bool home_board_mono(void)
{
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    return s.panel_model == HOME_PANEL_NOTE4;
}

const char *home_board_model_name(void)
{
    qurantrix_settings_t s;
    qurantrix_settings_get(&s);
    if (s.panel_model == HOME_PANEL_NOTE4)
        return "NOTE4";
    if (s.panel_model == HOME_PANEL_NOTE4C)
        return "NOTE4C";
    return "NOTE4C";
}

int home_board_render_intensity(int cfg_intensity)
{
    if (home_board_mono())
        return 0;
    if (cfg_intensity < 0)
        return 0;
    if (cfg_intensity > 2)
        return 2;
    return cfg_intensity;
}
