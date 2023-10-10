#include <time.h>
#include <lvgl/lvgl.h>

#include "main.h"
#include "ui_resource.h"

#include "ui_intercom_homepage.h"

static lv_obj_t *btn_return;

static lv_obj_t *main = NULL;

static lv_obj_t *ui_box_area;
static lv_obj_t *ui_box_main;
static lv_obj_t *ui_box[10];

void intercom_call_ui_init();
void monitor_ui_init();
static void page_switch(lv_event_t *e);

static struct btn_desc button[] =
{
    {
        &ui_box[0],
        NULL,
        "视频监控",
        {0, 0, 1, 1},
        common_draw,
        page_switch,
        monitor_ui_init
    },
    {
        &ui_box[1],
        NULL,
        "对讲呼叫",
        {1, 0, 2, 1},
        common_draw,
        page_switch,
        intercom_call_ui_init
    },
    {
        &ui_box[2],
        NULL,
        "安防报警",
        {0, 1, 1, 2},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[3],
        NULL,
        "信息",
        {1, 1, 2, 2},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[4],
        NULL,
        "家电控制",
        {0, 2, 1, 3},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[5],
        NULL,
        "留影留言",
        {1, 2, 2, 3},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[6],
        NULL,
        "电梯召唤",
        {0, 3, 1, 4},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[7],
        NULL,
        "呼叫管理员",
        {1, 3, 2, 4},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[8],
        NULL,
        "图片管理",
        {0, 4, 1, 5},
        common_draw,
        page_switch,
        NULL
    },
    {
        &ui_box[9],
        NULL,
        "家人留言",
        {1, 4, 2, 5},
        common_draw,
        page_switch,
        NULL
    },
};

static lv_coord_t col_dsc[] = {200, 200, LV_GRID_TEMPLATE_LAST};
static lv_coord_t row_dsc[] = {200, 200, 200, 200, 200, 200, 200, 200, 200, 200, LV_GRID_TEMPLATE_LAST};

struct btn_matrix_desc btn_desc = {
    .col_dsc = col_dsc,
    .row_dsc = row_dsc,
    .pad = 5,
    .gap = 20,
    .desc = button,
    .btn_cnt = sizeof(button) / sizeof(button[0]),
};

static void page_switch(lv_event_t *e)
{
    void (*func)(void) = lv_event_get_user_data(e);

    if (func)
    {
        func();
        lv_obj_del(main);
        main = NULL;
    }
}

static void btn_return_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);
    if (code == LV_EVENT_CLICKED)
    {
        home_ui_init();
        lv_obj_del(main);
        main = NULL;
    }
}

void intercom_homepage_ui_init()
{
    if (main)
        return;

    main = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(main);
    lv_obj_set_style_pad_all(main, 10, LV_PART_MAIN);
    lv_obj_set_size(main, lv_pct(100), lv_pct(100));
    lv_obj_refr_size(main);

    btn_return = ui_return_btn_create(main, btn_return_cb, "楼宇对讲");

    ui_box_area = lv_obj_create(main);
    lv_obj_remove_style_all(ui_box_area);
    lv_obj_set_size(ui_box_area, lv_pct(100), lv_pct(90));
    lv_obj_align(ui_box_area, LV_ALIGN_BOTTOM_MID, 0, 0);

    ui_box_main = ui_btnmatrix_create(ui_box_area, &btn_desc);
    lv_obj_center(ui_box_main);
}

