#if USE_RKADK
#include <lvgl/lvgl.h>
#include <lv_drivers/rkadk/rkadk.h>

void hal_rkadk_init(lv_coord_t hor_res, lv_coord_t ver_res, int rotated)
{
    /*Create a display*/
    rkadk_disp_drv_init(rotated);
}
#endif

