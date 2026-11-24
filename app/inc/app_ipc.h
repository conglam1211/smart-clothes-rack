#ifndef APP_IPC_H
#define APP_IPC_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

typedef struct {
    uint8_t hour, minute, second;
} ClockTime;

typedef struct {
    uint16_t start, end; /* So phut tinh tu 00:00. */
} Schedule;

typedef struct {
    uint32_t id;
    Schedule schedule;
    uint16_t clock_minutes;
    uint8_t clock_seconds;
    bool clock_changed, schedule_changed;
} SaveRequest;

typedef struct {
    ClockTime clock;
    uint32_t save_id;
    uint8_t temperature, humidity;
    bool rtc_ok, rtc_stopped, dht_ok;
    bool save_ok, clock_saved;
} SensorData;

typedef enum {
    HOME, RTC_HOUR, RTC_MINUTE, RTC_SECOND,
    START_HOUR, START_MINUTE, END_HOUR, END_MINUTE
} MenuField;

typedef enum {
    MENU_READY, MENU_SAVING, MENU_EQUAL_TIMES, MENU_SAVE_ERROR
} MenuStatus;

typedef struct {
    Schedule schedule;
    SaveRequest draft;
    SensorData sensors;
    MenuField field;
    MenuStatus status;
    bool automatic, raining, rack_open, rack_moving, moving_open;
} DisplayData;

/* Cac bien nay se duoc dinh nghia trong app_ipc.c khi tich hop. */
extern QueueHandle_t sensor_queue;
extern QueueHandle_t display_queue;
extern QueueHandle_t save_queue;
extern SemaphoreHandle_t i2c_mutex;

bool app_ipc_init(void);

#endif
