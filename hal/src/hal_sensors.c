/* DS1307 and DHT11 driver. RTC APIs serialize I2C with a finite timeout. */
#include "app_ipc.h"
#include "hardware.h"
#include "hal_devices.h"
#include "task.h"

#define DS1307_ADDRESS  0x68u
#define SCHEDULE_REG    0x08u
#define SCHEDULE_MARKER 0xA7u

static bool bus_take(void)
{
    /* At boot, before app_ipc_init(), no task can contend for the bus. */
    return i2c_mutex == NULL ||
           xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(45)) == pdTRUE;
}

static void bus_give(void)
{
    if (i2c_mutex != NULL) {
        xSemaphoreGive(i2c_mutex);
    }
}

static bool from_bcd(uint8_t bcd, uint8_t max, uint8_t *out)
{
    uint8_t low = bcd & 0x0Fu;
    uint8_t high = bcd >> 4;
    uint8_t value = (uint8_t)(high * 10u + low);
    if (low > 9u || high > 9u || value > max) {
        return false;
    }
    *out = value;
    return true;
}

static uint8_t to_bcd(unsigned int value)
{
    return (uint8_t)(((value / 10u) << 4) | (value % 10u));
}

static bool rtc_get_time_unlocked(ClockTime *out, bool *stopped)
{
    uint8_t bytes[3];
    ClockTime time;
    *stopped = false;
    if (!i2c_read_block(DS1307_ADDRESS, 0, bytes, sizeof bytes)) {
        return false;
    }
    *stopped = (bytes[0] & 0x80u) != 0u;
    if (*stopped
        || !from_bcd(bytes[0], 59, &time.second)
        || !from_bcd(bytes[1] & 0x7Fu, 59, &time.minute)) {
        return false;
    }
    if ((bytes[2] & 0x40u) != 0u) {
        if (!from_bcd(bytes[2] & 0x1Fu, 12, &time.hour) || time.hour == 0u) {
            return false;
        }
        time.hour = (uint8_t)((time.hour % 12u) + ((bytes[2] & 0x20u) ? 12u : 0u));
    } else if (!from_bcd(bytes[2] & 0x3Fu, 23, &time.hour)) {
        return false;
    }
    *out = time;
    return true;
}

static bool rtc_set_time_unlocked(uint16_t minutes, uint8_t seconds)
{
    /* CH = 0 de khoi dong dao dong, gio theo che do 24h. */
    const uint8_t bytes[] = { to_bcd(seconds), to_bcd(minutes % 60u),
                              to_bcd(minutes / 60u) };
    return i2c_write_block(DS1307_ADDRESS, 0, bytes, sizeof bytes);
}

static uint8_t schedule_check(const uint8_t bytes[6])
{
    return (uint8_t)(0x5Au ^ bytes[0] ^ bytes[1] ^ bytes[2] ^ bytes[3] ^ bytes[4]);
}

static bool rtc_load_schedule_unlocked(Schedule *schedule)
{
    uint8_t bytes[6];
    if (!i2c_read_block(DS1307_ADDRESS, SCHEDULE_REG, bytes, sizeof bytes)
        || bytes[0] != SCHEDULE_MARKER || bytes[5] != schedule_check(bytes)) {
        return false;
    }
    Schedule stored = {
        (uint16_t)(bytes[1] | ((uint16_t)bytes[2] << 8)),
        (uint16_t)(bytes[3] | ((uint16_t)bytes[4] << 8)),
    };
    if (stored.start >= 1440u || stored.end >= 1440u || stored.start == stored.end) {
        return false;
    }
    *schedule = stored;
    return true;
}

static bool rtc_save_schedule_unlocked(Schedule schedule)
{
    uint8_t bytes[6] = {
        SCHEDULE_MARKER, (uint8_t)schedule.start, (uint8_t)(schedule.start >> 8),
        (uint8_t)schedule.end, (uint8_t)(schedule.end >> 8), 0
    };
    bytes[5] = schedule_check(bytes);
    Schedule stored;
    return i2c_write_block(DS1307_ADDRESS, SCHEDULE_REG, bytes, sizeof bytes)
        && rtc_load_schedule_unlocked(&stored) && (stored.start == schedule.start && stored.end == schedule.end);
}

static bool dht11_read(SensorData *data)
{
    uint8_t bytes[5] = { 0 };
    GPIOA_BSRR = DHT_PIN << 16;
    gpio_configure(&GPIOA_CRL, DHT_GPIO, GPIO_OPEN_DRAIN);
    vTaskDelay(pdMS_TO_TICKS(20) + 1u);

    taskENTER_CRITICAL();
    GPIOA_BSRR = DHT_PIN;
    gpio_configure(&GPIOA_CRL, DHT_GPIO, GPIO_INPUT_PULL);
    bool ok = wait_level(&GPIOA_IDR, DHT_PIN, false, 140)
           && wait_level(&GPIOA_IDR, DHT_PIN, true, 140)
           && wait_level(&GPIOA_IDR, DHT_PIN, false, 140);
    for (unsigned int i = 0; ok && i < 40u; ++i) {
        ok = wait_level(&GPIOA_IDR, DHT_PIN, true, 110);
        if (!ok) {
            break;
        }
        uint16_t start = micros();
        ok = wait_level(&GPIOA_IDR, DHT_PIN, false, 120);
        bytes[i / 8u] = (uint8_t)((bytes[i / 8u] << 1)
                        | ((uint16_t)(micros() - start) >= 45u ? 1u : 0u));
    }
    taskEXIT_CRITICAL();

    uint8_t sum = (uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]);
    if (!ok || sum != bytes[4] || bytes[0] > 100u || bytes[2] > 99u) {
        return false;
    }
    data->humidity = bytes[0];
    data->temperature = bytes[2];
    return true;
}


bool hal_rtc_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second, bool *stopped)
{
    if (hour == NULL || minute == NULL || second == NULL || stopped == NULL) {
        return false;
    }
    *stopped = false;
    if (!bus_take()) return false;
    ClockTime time;
    bool ok = rtc_get_time_unlocked(&time, stopped);
    bus_give();
    if (ok) {
        *hour = time.hour;
        *minute = time.minute;
        *second = time.second;
    }
    return ok;
}

bool hal_rtc_set_time(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (hour > 23u || minute > 59u || second > 59u || !bus_take()) return false;
    bool ok = rtc_set_time_unlocked((uint16_t)(hour * 60u + minute), second);
    bus_give();
    return ok;
}

bool hal_rtc_load_schedule(uint16_t *start, uint16_t *end)
{
    if (start == NULL || end == NULL || !bus_take()) return false;
    Schedule schedule;
    bool ok = rtc_load_schedule_unlocked(&schedule);
    bus_give();
    if (ok) {
        *start = schedule.start;
        *end = schedule.end;
    }
    return ok;
}

bool hal_rtc_save_schedule(uint16_t start, uint16_t end)
{
    if (start >= 1440u || end >= 1440u || start == end || !bus_take()) return false;
    Schedule schedule = { start, end };
    bool ok = rtc_save_schedule_unlocked(schedule);
    bus_give();
    return ok;
}

bool hal_dht11_read(uint8_t *temperature, uint8_t *humidity)
{
    if (temperature == NULL || humidity == NULL) return false;
    SensorData data = { 0 };
    if (!dht11_read(&data)) return false;
    *temperature = data.temperature;
    *humidity = data.humidity;
    return true;
}
