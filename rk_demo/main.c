/*
 * Copyright (c) 2023 Rockchip, Inc. All Rights Reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include <fcntl.h>
#include <lvgl/lvgl.h>
#include <lvgl/lv_conf.h>

#include "main.h"
#include "hal_sdl.h"
#include "hal_drm.h"
#include "home_ui.h"
#include "ui_resource.h"

#include "hal_rkadk.h"

#if ROCKIT_EN
#include "rk_defines.h"
#include "rk_mpi_sys.h"
#endif

#include "wifibt.h"

lv_ft_info_t ttf_main_s;
lv_ft_info_t ttf_main_m;
lv_ft_info_t ttf_main_l;

lv_style_t style_txt_s;
lv_style_t style_txt_m;
lv_style_t style_txt_l;

lv_dir_t scr_dir;
lv_coord_t scr_w;
lv_coord_t scr_h;

static int g_indev_rotation = 0;
static int g_disp_rotation = LV_DISP_ROT_NONE;

static int quit = 0;

#if USE_SENSOR
#define SYS_NODE    "/sys/devices/platform/backlight/backlight/backlight/brightness"
static lv_timer_t *lsensor_timer;
static lv_timer_t *psensor_timer;
static lv_timer_t *backlight_timer;
/* light sensor level from 0 to 7 */
static int brightness[8] = {45, 75, 105, 135, 165, 195, 225, 255};
static int backlight_en = 1;
static int backlight_level = ARRAY_SIZE(brightness) - 1;
static int backlight_fd = -1;
#endif

extern void rk_demo_init(void);

static void sigterm_handler(int sig)
{
    fprintf(stderr, "signal %d\n", sig);
    quit = 1;
}

int app_disp_rotation(void)
{
    return g_disp_rotation;
}

static void check_scr(void)
{
    scr_w = LV_HOR_RES;
    scr_h = LV_VER_RES;

    if (scr_w > scr_h)
        scr_dir = LV_DIR_HOR;
    else
        scr_dir = LV_DIR_VER;

    printf("%s %dx%d\n", __func__, scr_w, scr_h);
}

#if USE_SENSOR
static int level_to_brightness(int level)
{
    if (level < 0)
        level = 0;
    if (level >= ARRAY_SIZE(brightness))
        level > ARRAY_SIZE(brightness) - 1;

    return brightness[level];
}

static void update_backlight(int en, int value)
{
    static uint8_t last_value = 0;
    uint8_t final_value;
    char val[8];

    if (backlight_fd <= 0)
        return;

    if (en)
    {
        final_value = value;
    }
    else
    {
        final_value = 0;
    }
    if (last_value == final_value)
        return;
    last_value = final_value;
    snprintf(val, sizeof(val), "%d", final_value);
    if (write(backlight_fd, &val, strlen(val)) <= 0)
        LV_LOG_ERROR("update backlight failed");
}

static void backlight_cb(lv_timer_t *timer)
{
    static int cur_en = 1;
    static int cur_value = 255;
    static int start_tick = 0;
    int value_step;
    int target_value;

    target_value = level_to_brightness(backlight_level);
    if (cur_value != target_value)
    {
        if (cur_value > target_value)
        {
            value_step = (int)((cur_value - target_value) / 2.0);
            if (value_step == 0)
                value_step = 2;
            if ((cur_value - value_step) < target_value)
                cur_value = target_value;
            else
                cur_value -= value_step;
        }
        else
        {
            value_step = (int)((target_value - cur_value) / 2.0);
            if (value_step == 0)
                value_step = 2;
            if ((cur_value + value_step) > target_value)
                cur_value = target_value;
            else
                cur_value += value_step;
        }
    }

    if (cur_en != backlight_en)
    {
        if (cur_en == 0)
        {
            cur_en = backlight_en;
        }
        else
        {
            if (start_tick == 0)
                start_tick = lv_tick_get();
            else if (lv_tick_elaps(start_tick) > 3000)
                cur_en = backlight_en;
        }
    }
    else
    {
        start_tick = 0;
    }

    update_backlight(cur_en, cur_value);
}

static void lsensor_cb(lv_timer_t *timer)
{
    lv_indev_drv_t *drv = (lv_indev_drv_t *)timer->user_data;
    lv_indev_data_t data;

    drv->read_cb(drv, &data);
    if (data.continue_reading)
        return;
    LV_LOG_TRACE("%d %u", data.state, data.key);
    backlight_level = data.key;
}

static void psensor_cb(lv_timer_t *timer)
{
    lv_indev_drv_t *drv = (lv_indev_drv_t *)timer->user_data;
    lv_indev_data_t data;

    memset(&data, 0, sizeof(data));
    drv->read_cb(drv, &data);
    if (data.continue_reading)
        return;
    LV_LOG_TRACE("%d", data.state);
    backlight_en = data.state;
}
#endif

static void lvgl_init(void)
{
    lv_init();

#if USE_SDL_GPU
    hal_sdl_init(0, 0, g_disp_rotation);
#endif

#if USE_DRM
    hal_drm_init(0, 0, g_disp_rotation);
#endif

#if USE_RKADK
    hal_rkadk_init(0, 0, g_disp_rotation);
#endif

#if USE_EVDEV || USE_SENSOR
    lv_port_indev_init(g_indev_rotation);
#endif

#if USE_SENSOR
    lv_indev_drv_t *lsensor, *psensor;
    lsensor = lv_port_indev_get_lsensor_drv();
    psensor = lv_port_indev_get_psensor_drv();
    if ((lsensor && lsensor->read_cb) &&
            (psensor && psensor->read_cb))
    {
        lsensor_timer = lv_timer_create(lsensor_cb, 100, lsensor);
        psensor_timer = lv_timer_create(psensor_cb, 100, psensor);
        backlight_timer = lv_timer_create(backlight_cb, 50, NULL);
        backlight_fd = open(SYS_NODE, O_RDWR);
        if (backlight_fd <= 0)
            LV_LOG_ERROR("open backlight node failed");
        update_backlight(backlight_en, brightness[backlight_level]);
        backlight_en = 0;
    }
#endif

    check_scr();
}

static void font_init(void)
{
    lv_freetype_init(64, 1, 0);

    if (scr_dir == LV_DIR_HOR)
    {
        ttf_main_s.weight = ALIGN(RK_PCT_W(2), 2);
        ttf_main_m.weight = ALIGN(RK_PCT_W(3), 2);
        ttf_main_l.weight = ALIGN(RK_PCT_W(9), 2);
    }
    else
    {
        ttf_main_s.weight = ALIGN(RK_PCT_H(2), 2);
        ttf_main_m.weight = ALIGN(RK_PCT_H(3), 2);
        ttf_main_l.weight = ALIGN(RK_PCT_H(9), 2);
    }

    printf("%s s %d m %d l %d\n", __func__,
           ttf_main_s.weight, ttf_main_m.weight, ttf_main_l.weight);

    ttf_main_s.name = MAIN_FONT;
    ttf_main_s.style = FT_FONT_STYLE_NORMAL;
    lv_ft_font_init(&ttf_main_s);

    ttf_main_m.name = MAIN_FONT;
    ttf_main_m.style = FT_FONT_STYLE_NORMAL;
    lv_ft_font_init(&ttf_main_m);

    ttf_main_l.name = MAIN_FONT;
    ttf_main_l.style = FT_FONT_STYLE_NORMAL;
    lv_ft_font_init(&ttf_main_l);
}

static void style_init(void)
{
    lv_style_init(&style_txt_s);
    lv_style_set_text_font(&style_txt_s, ttf_main_s.font);
    lv_style_set_text_color(&style_txt_s, lv_color_black());

    lv_style_init(&style_txt_m);
    lv_style_set_text_font(&style_txt_m, ttf_main_m.font);
    lv_style_set_text_color(&style_txt_m, lv_color_black());

    lv_style_init(&style_txt_l);
    lv_style_set_text_font(&style_txt_l, ttf_main_l.font);
    lv_style_set_text_color(&style_txt_l, lv_color_black());
}

void app_init(void)
{
    font_init();
    style_init();
}

int main(int argc, char **argv)
{
#define FPS     0
#if FPS
    float maxfps = 0.0, minfps = 1000.0;
    float fps;
    float fps0 = 0, fps1 = 0;
    uint32_t st, et;
    uint32_t st0 = 0, et0;
#endif
    signal(SIGINT, sigterm_handler);

#if ROCKIT_EN
    RK_MPI_SYS_Init();
#endif
#if WIFIBT_EN
    run_wifibt_server();
#endif

    lvgl_init();

    app_init();

    rk_demo_init();

    while (!quit)
    {
#if FPS
        st = clock_ms();
#endif
        lv_task_handler();
#if FPS
        et = clock_ms();
        fps = 1000 / (et - st);
        if (fps != 0.0 && fps < minfps)
        {
            minfps = fps;
            printf("Update minfps %f\n", minfps);
        }
        if (fps < 60 && fps > maxfps)
        {
            maxfps = fps;
            printf("Update maxfps %f\n", maxfps);
        }
        if (fps > 0.0 && fps < 60)
        {
            fps0 = (fps0 + fps) / 2;
            fps1 = (fps0 + fps1) / 2;
        }
        et0 = clock_ms();
        if ((et0 - st0) > 1000)
        {
            printf("avg:%f\n", fps1);
            st0 = et0;
        }
#endif
        usleep(100);
    }

#if ROCKIT_EN
    RK_MPI_SYS_Exit();
#endif

    return 0;
}
