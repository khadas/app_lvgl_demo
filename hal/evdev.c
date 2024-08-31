/**
 * @file evdev.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "evdev.h"
#if USE_EVDEV != 0 || USE_BSD_EVDEV

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#if USE_BSD_EVDEV
#include <dev/dev/input.h>
#else
#include <linux/input.h>
#endif
#include <libevdev-1.0/libevdev/libevdev.h>
#include <dirent.h>

#include "main.h"
/*********************
 *      DEFINES
 *********************/

#define PSENSOR_IOCTL_MAGIC             'p'
#define PSENSOR_IOCTL_GET_ENABLED       _IOR(PSENSOR_IOCTL_MAGIC, 1, int *)
#define PSENSOR_IOCTL_ENABLE            _IOW(PSENSOR_IOCTL_MAGIC, 2, int *)
#define PSENSOR_IOCTL_DISABLE           _IOW(PSENSOR_IOCTL_MAGIC, 3, int *)

#define LIGHTSENSOR_IOCTL_MAGIC         'l'
#define LIGHTSENSOR_IOCTL_GET_ENABLED   _IOR(LIGHTSENSOR_IOCTL_MAGIC, 1, int *)
#define LIGHTSENSOR_IOCTL_ENABLE        _IOW(LIGHTSENSOR_IOCTL_MAGIC, 2, int *)
#define LIGHTSENSOR_IOCTL_SET_RATE      _IOW(LIGHTSENSOR_IOCTL_MAGIC, 3, short)

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max);

/**********************
 *  STATIC VARIABLES
 **********************/
struct libevdev *evdev = NULL;
int evdev_fd = -1;
int evdev_root_x;
int evdev_root_y;
int evdev_button;
int evdev_min_x = DEFAULT_EVDEV_HOR_MIN;
int evdev_max_x = DEFAULT_EVDEV_HOR_MAX;
int evdev_min_y = DEFAULT_EVDEV_VER_MIN;
int evdev_max_y = DEFAULT_EVDEV_VER_MAX;
int evdev_calibrate = 0;

int evdev_key_val;

int evdev_rot;

#if USE_SENSOR
static int psensor_event_id = -1;
static int lsensor_event_id = -1;
static int psensor_fd = -1;
static int lsensor_fd = -1;
#endif
/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

#define TP_NAME_LEN (32)
static char tp_event[TP_NAME_LEN] = EVDEV_NAME;
/**
 * Get touchscreen device event no
 */
int evdev_get_tp_event(void)
{
    int fd = 0, len = 0;
    int i = 0, input_dev_num = 0;
    DIR *pDir;
    struct dirent *ent = NULL;
    char file_name[TP_NAME_LEN];
    char tp_name[TP_NAME_LEN];
    char *path = "/sys/class/input";

    if ((pDir = opendir(path)) == NULL)
    {
        printf("%s: open %s filed\n", __func__, path);
        return -1;
    }

    while ((ent = readdir(pDir)) != NULL)
    {
        if (strstr(ent->d_name, "input"))
            input_dev_num++;
//            printf("%s: %s input deveices %d\n",
//                   __func__,
//                   ent->d_name,
//                   input_dev_num);
    }
    closedir(pDir);

    for (i = 0; i < input_dev_num; i++)
    {
        sprintf(file_name, "/sys/class/input/input%d/name", i);
        fd = open(file_name, O_RDONLY);
        if (fd == -1)
        {
            printf("%s: open %s failed\n", __func__, file_name);
            continue;
        }

        len = read(fd, tp_name, TP_NAME_LEN);
        close(fd);
        if (len <= 0)
        {
            printf("%s: read %s failed\n", __func__, file_name);
            continue;
        }

        if (len >= TP_NAME_LEN)
            len = TP_NAME_LEN - 1;

        tp_name[len] = '\0';

#if USE_SENSOR
        if (strstr(tp_name, "lightsensor"))
        {
            lsensor_event_id = i;
            continue;
        }

        if (strstr(tp_name, "proximity"))
        {
            psensor_event_id = i;
            continue;
        }
#else
        /*
         * There is a 'ts' in the 'lightsensor', skip it to avoid being
         * treated as a touch device
         */
        if (strstr(tp_name, "lightsensor"))
            continue;
#endif

        if (strstr(tp_name, "ts") || strstr(tp_name, "gsl"))
        {
            sprintf(tp_event, "/dev/input/event%d", i);
            printf("%s: %s = %s%s\n", __func__, file_name, tp_name, tp_event);
            return 0;
        }
    }

    return -1;
}

#if USE_SENSOR
void evdev_sensor_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct input_event in;
    struct timeval tv;
    int drv_fd = *(int *)drv->user_data;
    fd_set rdfs;
    int ret;

    FD_ZERO(&rdfs);
    FD_SET(drv_fd, &rdfs);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    data->continue_reading = 1;
    if (select(drv_fd + 1, &rdfs, NULL, NULL, &tv) <= 0)
    {
        return;
    }

    if ((ret = read(drv_fd, &in, sizeof(in))) > 0)
    {
        if (in.type == EV_ABS)
        {
            if (in.code == ABS_DISTANCE)
            {
                if (in.value == 0)
                    data->state = LV_INDEV_STATE_RELEASED;
                else
                    data->state = LV_INDEV_STATE_PRESSED;
                data->continue_reading = 0;
            }
            else if (in.code == ABS_MISC)
            {
                if (in.value == 0)
                    data->state = LV_INDEV_STATE_RELEASED;
                else
                    data->state = LV_INDEV_STATE_PRESSED;
                data->key = in.value;
                data->continue_reading = 0;
            }
        }
    }
}

void *evdev_get_psensor(void)
{
    return &psensor_fd;
}

int evdev_init_psensor(void)
{
    char event_name[64];
    int fd, ret, enable = 1;

    if (psensor_event_id == -1)
    {
        if (evdev_get_tp_event() < 0)
        {
            printf("%s get event failed\n", __func__);
            return -1;
        }
        if (psensor_event_id == -1)
        {
            printf("%s get psensor event failed\n", __func__);
            return -1;
        }
    }

    fd = open("/dev/psensor", O_RDWR);
    if (fd < 0)
    {
        printf("can't open /dev/psensor!\n");
        return -1;
    }

    ret = ioctl(fd, PSENSOR_IOCTL_ENABLE, &enable);
    if (ret < 0)
    {
        printf("eanble /dev/psensor failed %d!\n", ret);
    }
    else
    {
        printf("enable /dev/psensor successfully!\n");
    }
    close(fd);

    snprintf(event_name, sizeof(event_name),
             "/dev/input/event%d", psensor_event_id);
    fd = open(event_name, O_RDONLY);
    if (fd < 0)
    {
        printf("can't open %s\n", event_name);
        return -1;
    }
    psensor_fd = fd;

    return ret;
}

void *evdev_get_lsensor(void)
{
    return &lsensor_fd;
}

int evdev_init_lsensor(void)
{
    char event_name[64];
    int fd, ret, enable = 1;

    if (lsensor_event_id == -1)
    {
        if (evdev_get_tp_event() < 0)
        {
            printf("%s get event failed\n", __func__);
            return -1;
        }
        if (lsensor_event_id == -1)
        {
            printf("%s get lsensor event failed\n", __func__);
            return -1;
        }
    }

    fd = open("/dev/lightsensor", O_RDWR);
    if (fd < 0)
    {
        printf("can't open /dev/lightsensor!\n");
        return -1;
    }

    ret = ioctl(fd, LIGHTSENSOR_IOCTL_ENABLE, &enable);
    if (ret < 0)
    {
        printf("eanble /dev/lightsensor failed %d!\n", ret);
    }
    else
    {
        printf("enable /dev/lightsensor successfully!\n");
    }
    close(fd);

    snprintf(event_name, sizeof(event_name),
             "/dev/input/event%d", lsensor_event_id);
    fd = open(event_name, O_RDONLY);
    if (fd < 0)
    {
        printf("can't open %s\n", event_name);
        return -1;
    }
    lsensor_fd = fd;

    return ret;
}
#endif

/**
 * Initialize the evdev interface
 */
int evdev_init(lv_disp_drv_t *drv, int rot)
{
    int rc = 1;
    evdev_rot = rot;
    if (evdev_get_tp_event() < 0)
    {
        printf("%s get tp event failed\n", __func__);
        return -1;
    }
    return evdev_set_file(drv, tp_event);
}
/**
 * reconfigure the device file for evdev
 * @param dev_name set the evdev device filename
 * @return true: the device file set complete
 *         false: the device file doesn't exist current system
 */
int evdev_set_file(lv_disp_drv_t *drv, char *dev_name)
{
    int disp_hor;
    int disp_ver;
    int rc = 1;

    if (evdev_fd != -1)
        close(evdev_fd);
    evdev_fd = -1;
    if (evdev)
        libevdev_free(evdev);
    evdev = NULL;

#if USE_BSD_EVDEV
    evdev_fd = open(dev_name, O_RDWR | O_NOCTTY);
#else
    evdev_fd = open(dev_name, O_RDWR | O_NOCTTY | O_NDELAY);
#endif

    if (evdev_fd == -1)
    {
        perror("unable open evdev interface:");
        return -1;
    }

    rc = libevdev_new_from_fd(evdev_fd, &evdev);
    if (rc < 0)
    {
        printf("Failed to init libevdev (%s)\n", strerror(-rc));
    }
    else
    {
        if (libevdev_has_event_type(evdev, EV_ABS))
        {
            const struct input_absinfo *abs;
            if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_X))
            {
                abs = libevdev_get_abs_info(evdev, ABS_MT_POSITION_X);
                printf("EV_ABS ABS_MT_POSITION_X\n");
                printf("\tMin\t%6d\n", abs->minimum);
                printf("\tMax\t%6d\n", abs->maximum);
                evdev_min_x = abs->minimum;
                evdev_max_x = abs->maximum;
            }
            if (libevdev_has_event_code(evdev, EV_ABS, ABS_MT_POSITION_Y))
            {
                abs = libevdev_get_abs_info(evdev, ABS_MT_POSITION_Y);
                printf("EV_ABS ABS_MT_POSITION_Y\n");
                printf("\tMin\t%6d\n", abs->minimum);
                printf("\tMax\t%6d\n", abs->maximum);
                evdev_min_y = abs->minimum;
                evdev_max_y = abs->maximum;
            }
        }
    }

#if USE_BSD_EVDEV
    fcntl(evdev_fd, F_SETFL, O_NONBLOCK);
#else
    fcntl(evdev_fd, F_SETFL, O_ASYNC | O_NONBLOCK);
#endif

    evdev_root_x = 0;
    evdev_root_y = 0;
    evdev_key_val = 0;
    evdev_button = LV_INDEV_STATE_REL;

    if ((evdev_rot == 0) || (evdev_rot == 180))
    {
        disp_hor = drv->hor_res;
        disp_ver = drv->ver_res;
    }
    else
    {
        disp_hor = drv->ver_res;
        disp_ver = drv->hor_res;
    }
    if ((evdev_min_x != 0) ||
            (evdev_max_x != disp_hor) ||
            (evdev_min_y != 0) ||
            (evdev_max_y != disp_ver))
    {
        evdev_calibrate = 1;
    }
    printf("evdev_calibrate = %d\n", evdev_calibrate);

    return 0;
}
/**
 * Get the current position and state of the evdev
 * @param data store the evdev data here
 * @return false: because the points are not buffered, so no more data to be read
 */
void evdev_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct input_event in;
    int tmp;

    while (read(evdev_fd, &in, sizeof(struct input_event)) > 0)
    {
        if (in.type == EV_REL)
        {
            if (in.code == REL_X)
#if EVDEV_SWAP_AXES
                evdev_root_y += in.value;
#else
                evdev_root_x += in.value;
#endif
            else if (in.code == REL_Y)
#if EVDEV_SWAP_AXES
                evdev_root_x += in.value;
#else
                evdev_root_y += in.value;
#endif
        }
        else if (in.type == EV_ABS)
        {
            if (in.code == ABS_X)
#if EVDEV_SWAP_AXES
                evdev_root_y = in.value;
#else
                evdev_root_x = in.value;
#endif
            else if (in.code == ABS_Y)
#if EVDEV_SWAP_AXES
                evdev_root_x = in.value;
#else
                evdev_root_y = in.value;
#endif
            else if (in.code == ABS_MT_POSITION_X)
#if EVDEV_SWAP_AXES
                evdev_root_y = in.value;
#else
                evdev_root_x = in.value;
#endif
            else if (in.code == ABS_MT_POSITION_Y)
#if EVDEV_SWAP_AXES
                evdev_root_x = in.value;
#else
                evdev_root_y = in.value;
#endif
            else if (in.code == ABS_MT_TRACKING_ID)
            {
                if (in.value == -1)
                    evdev_button = LV_INDEV_STATE_REL;
                else if ((in.value == 0) || (in.value == 1))
                    evdev_button = LV_INDEV_STATE_PR;
            }
        }
        else if (in.type == EV_KEY)
        {
            if (in.code == BTN_MOUSE || in.code == BTN_TOUCH)
            {
                if (in.value == 0)
                    evdev_button = LV_INDEV_STATE_REL;
                else if (in.value == 1)
                    evdev_button = LV_INDEV_STATE_PR;
            }
            else if (drv->type == LV_INDEV_TYPE_KEYPAD)
            {
                data->state = (in.value) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
                switch (in.code)
                {
                case KEY_BACKSPACE:
                    data->key = LV_KEY_BACKSPACE;
                    break;
                case KEY_ENTER:
                    data->key = LV_KEY_ENTER;
                    break;
                case KEY_UP:
                    data->key = LV_KEY_UP;
                    break;
                case KEY_LEFT:
                    data->key = LV_KEY_PREV;
                    break;
                case KEY_RIGHT:
                    data->key = LV_KEY_NEXT;
                    break;
                case KEY_DOWN:
                    data->key = LV_KEY_DOWN;
                    break;
                default:
                    data->key = 0;
                    break;
                }
                evdev_key_val = data->key;
                evdev_button = data->state;
                return ;
            }
        }
    }

    if (drv->type == LV_INDEV_TYPE_KEYPAD)
    {
        /* No data retrieved */
        data->key = evdev_key_val;
        data->state = evdev_button;
        return ;
    }
    if (drv->type != LV_INDEV_TYPE_POINTER)
        return ;
    /*Store the collected data*/

    if (evdev_calibrate)
    {
        data->point.x = map(evdev_root_x,
                            evdev_min_x, evdev_max_x,
                            0, drv->disp->driver->hor_res);
        data->point.y = map(evdev_root_y,
                            evdev_min_y, evdev_max_y,
                            0, drv->disp->driver->ver_res);
    }
    else
    {
        data->point.x = evdev_root_x;
        data->point.y = evdev_root_y;
    }

    data->state = evdev_button;

    switch (evdev_rot)
    {
    case 0:
    default:
        break;
    case 90:
        tmp = data->point.x;
        data->point.x = data->point.y;
        data->point.y = drv->disp->driver->ver_res - tmp;
        break;
    case 180:
        tmp = data->point.x;
        data->point.x = drv->disp->driver->hor_res - data->point.y;
        data->point.y = drv->disp->driver->ver_res - tmp;
        break;
    case 270:
        tmp = data->point.x;
        data->point.x = drv->disp->driver->hor_res - data->point.y;
        data->point.y = tmp;
        break;
    }

    if (data->point.x < 0)
        data->point.x = 0;
    if (data->point.y < 0)
        data->point.y = 0;
    if (data->point.x >= drv->disp->driver->hor_res)
        data->point.x = drv->disp->driver->hor_res - 1;
    if (data->point.y >= drv->disp->driver->ver_res)
        data->point.y = drv->disp->driver->ver_res - 1;

    return ;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/
int map(int x, int in_min, int in_max, int out_min, int out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

#endif
