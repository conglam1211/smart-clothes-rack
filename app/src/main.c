/*
 * STM32F103C8T6 (Blue Pill), 8 MHz HSI, FreeRTOS.
 * Dung voi startup.c, stm32f103.ld, FreeRTOSConfig.h va build.sh cua ban test.
 *
 * PB12: MODE/SET   PB13: UP/OPEN   PB14: DOWN/CLOSE
 * PB6: I2C SCL     PB7: I2C SDA    PA0: PWM SG90
 * PA4: DHT11 DATA  PA5: CAM BIEN MUA DO, muc 0 la co mua
 *
 * Man hinh chinh: bam MODE doi AUTO/MANUAL, giu MODE 1 giay de cai dat.
 * Menu: bam MODE chon muc, UP/DOWN thay doi gia tri, giu MODE de luu.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define RCC_APB2ENR REG32(0x40021018u)
#define RCC_APB1ENR REG32(0x4002101Cu)
#define GPIOA_CRL   REG32(0x40010800u)
#define GPIOA_IDR   REG32(0x40010808u)
#define GPIOA_BSRR  REG32(0x40010810u)
#define GPIOB_CRL   REG32(0x40010C00u)
#define GPIOB_CRH   REG32(0x40010C04u)
#define GPIOB_IDR   REG32(0x40010C08u)
#define GPIOB_BSRR  REG32(0x40010C10u)
#define TIM2_CR1    REG32(0x40000000u)
#define TIM2_EGR    REG32(0x40000014u)
#define TIM2_CCMR1  REG32(0x40000018u)
#define TIM2_CCER   REG32(0x40000020u)
#define TIM2_PSC    REG32(0x40000028u)
#define TIM2_ARR    REG32(0x4000002Cu)
#define TIM2_CCR1   REG32(0x40000034u)
#define TIM3_CR1    REG32(0x40000400u)
#define TIM3_EGR    REG32(0x40000414u)
#define TIM3_CNT    REG32(0x40000424u)
#define TIM3_PSC    REG32(0x40000428u)
#define TIM3_ARR    REG32(0x4000042Cu)

#define BIT(pin) (1u << (pin))
enum {
    SERVO_GPIO = 0, DHT_GPIO = 4, RAIN_GPIO = 5,
    SCL_GPIO = 6, SDA_GPIO = 7, MODE_GPIO = 12, UP_GPIO = 13, DOWN_GPIO = 14
};
#define DHT_PIN       BIT(DHT_GPIO)
#define RAIN_PIN      BIT(RAIN_GPIO)
#define SCL_PIN       BIT(SCL_GPIO)
#define SDA_PIN       BIT(SDA_GPIO)
#define MODE_PIN      BIT(MODE_GPIO)
#define UP_PIN        BIT(UP_GPIO)
#define DOWN_PIN      BIT(DOWN_GPIO)
#define GPIO_OPEN_DRAIN 0x6u
#define GPIO_INPUT_PULL 0x8u
#define GPIO_PWM_OUTPUT 0xBu
#define OLED_ADDRESS      0x3Cu
#define DS1307_ADDRESS    0x68u
#define SCHEDULE_REG      0x08u
#define SCHEDULE_MARKER   0xA7u
#define DEFAULT_START     (7u * 60u)
#define DEFAULT_END       (17u * 60u + 30u)
#define HIGH_HUMIDITY     85u
#define SERVO_STOP_US     1500u  /* SG90 360 do: xung trung tinh, dung quay. */
#define SERVO_OPEN_US     1700u  /* Quay mot chieu. Doi cho CLOSE neu bi nguoc. */
#define SERVO_CLOSE_US    1300u  /* Quay chieu con lai. */
#define SERVO_TRAVEL_MS    700u  /* Thu khong tai truoc, chinh theo hanh trinh that. */
#define BUTTON_DEBOUNCE_MS 30u
#define MODE_HOLD_MS       1000u
#define RAIN_RELEASE_MS    250u
#define DISPLAY_WIDTH     128u
#define DISPLAY_PAGES       8u
#define DISPLAY_COLUMNS    21u

typedef struct {
    uint8_t hour, minute, second;
} ClockTime;

typedef struct {
    uint16_t start, end;  /* So phut tinh tu 00:00. */
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

typedef enum { KEY_NONE, KEY_PRESS, KEY_TAP, KEY_HOLD } KeyEvent;

typedef struct {
    TickType_t changed_at, pressed_at;
    bool raw, stable, held;
} Button;

static QueueHandle_t sensor_queue, display_queue, save_queue;
static SemaphoreHandle_t i2c_mutex;
static Schedule boot_schedule = { DEFAULT_START, DEFAULT_END };

/* Hai ham duoc nhan FreeRTOS dung khi build bang -nostdlib. */
void *memcpy(void *destination, const void *source, size_t count)
{
    uint8_t *out = destination;
    const uint8_t *in = source;
    for (size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    uint8_t *out = destination;
    for (size_t i = 0; i < count; ++i) {
        out[i] = (uint8_t)value;
    }
    return destination;
}

static uint16_t micros(void)
{
    return (uint16_t)TIM3_CNT;
}

static void delay_us(uint16_t duration)
{
    uint16_t start = micros();
    while ((uint16_t)(micros() - start) < duration) {
    }
}

static bool wait_level(volatile const uint32_t *input, uint32_t pin,
                       bool high, uint16_t timeout_us)
{
    uint16_t start = micros();
    while (((*input & pin) != 0u) != high) {
        if ((uint16_t)(micros() - start) >= timeout_us) {
            return false;
        }
    }
    return true;
}

static void gpio_configure(volatile uint32_t *config, unsigned int pin, uint32_t mode)
{
    unsigned int shift = (pin % 8u) * 4u;
    *config = (*config & ~(0xFu << shift)) | (mode << shift);
}

static void hardware_init(void)
{
    RCC_APB2ENR |= BIT(2) | BIT(3);  /* GPIOA, GPIOB */
    RCC_APB1ENR |= BIT(0) | BIT(1);  /* TIM2, TIM3 */
    (void)RCC_APB1ENR;

    TIM3_PSC = 7;
    TIM3_ARR = 65535;
    TIM3_EGR = 1;
    TIM3_CR1 = 1;                   /* Dem tu do, moi tick 1 us. */

    GPIOA_BSRR = DHT_PIN | RAIN_PIN;
    gpio_configure(&GPIOA_CRL, DHT_GPIO, GPIO_INPUT_PULL);
    gpio_configure(&GPIOA_CRL, RAIN_GPIO, GPIO_INPUT_PULL);
    gpio_configure(&GPIOA_CRL, SERVO_GPIO, GPIO_PWM_OUTPUT);
    GPIOB_BSRR = MODE_PIN | UP_PIN | DOWN_PIN | SCL_PIN | SDA_PIN;
    gpio_configure(&GPIOB_CRH, MODE_GPIO, GPIO_INPUT_PULL);
    gpio_configure(&GPIOB_CRH, UP_GPIO, GPIO_INPUT_PULL);
    gpio_configure(&GPIOB_CRH, DOWN_GPIO, GPIO_INPUT_PULL);
    gpio_configure(&GPIOB_CRL, SCL_GPIO, GPIO_OPEN_DRAIN);
    gpio_configure(&GPIOB_CRL, SDA_GPIO, GPIO_OPEN_DRAIN);

    TIM2_PSC = 7;
    TIM2_ARR = 19999;               /* 20 ms, 50 Hz */
    TIM2_CCR1 = SERVO_STOP_US;
    TIM2_CCMR1 = (6u << 4) | BIT(3); /* PWM mode 1, preload CCR1 */
    TIM2_EGR = 1;
    TIM2_CCER = 1;
    TIM2_CR1 = 1;
}

/* I2C open-drain, giu chung mutex cho moi giao dich RTC/OLED. */
static bool i2c_raise_scl(void)
{
    GPIOB_BSRR = SCL_PIN;
    return wait_level(&GPIOB_IDR, SCL_PIN, true, 150);
}

static bool i2c_start(void)
{
    GPIOB_BSRR = SDA_PIN;
    delay_us(5);
    if (!i2c_raise_scl() || (GPIOB_IDR & SDA_PIN) == 0u) {
        return false;
    }
    delay_us(5);
    GPIOB_BSRR = SDA_PIN << 16;
    delay_us(5);
    GPIOB_BSRR = SCL_PIN << 16;
    return true;
}

static void i2c_stop(void)
{
    GPIOB_BSRR = (SCL_PIN | SDA_PIN) << 16;
    delay_us(5);
    (void)i2c_raise_scl();
    delay_us(5);
    GPIOB_BSRR = SDA_PIN;
    delay_us(5);
}

/* release_sda = 1 de gui bit 1 hoac de slave duoc phep lai SDA. */
static bool i2c_bit(bool release_sda, bool *sample)
{
    GPIOB_BSRR = release_sda ? SDA_PIN : (SDA_PIN << 16);
    delay_us(5);
    bool clock_ok = i2c_raise_scl();
    if (clock_ok) {
        delay_us(5);
        if (sample != NULL) {
            *sample = (GPIOB_IDR & SDA_PIN) != 0u;
        }
    }
    GPIOB_BSRR = SCL_PIN << 16;
    return clock_ok;
}

static bool i2c_write_byte(uint8_t byte)
{
    for (uint8_t mask = 0x80; mask != 0u; mask >>= 1) {
        if (!i2c_bit((byte & mask) != 0u, NULL)) {
            return false;
        }
    }
    bool nack;
    return i2c_bit(true, &nack) && !nack;
}

static bool i2c_read_byte(uint8_t *byte, bool ack)
{
    uint8_t result = 0;
    for (unsigned int bit = 0; bit < 8u; ++bit) {
        bool high;
        if (!i2c_bit(true, &high)) {
            return false;
        }
        result = (uint8_t)((result << 1) | (high ? 1u : 0u));
    }
    *byte = result;
    return i2c_bit(!ack, NULL);
}

static bool i2c_write_block(uint8_t address, uint8_t selector,
                            const uint8_t *bytes, size_t count)
{
    bool ok = i2c_start() && i2c_write_byte((uint8_t)(address << 1))
                          && i2c_write_byte(selector);
    for (size_t i = 0; ok && i < count; ++i) {
        ok = i2c_write_byte(bytes[i]);
    }
    i2c_stop();
    return ok;
}

static bool rtc_read_block(uint8_t reg, uint8_t *bytes, size_t count)
{
    bool ok = i2c_start() && i2c_write_byte(DS1307_ADDRESS << 1)
                          && i2c_write_byte(reg) && i2c_start()
                          && i2c_write_byte((DS1307_ADDRESS << 1) | 1u);
    for (size_t i = 0; ok && i < count; ++i) {
        ok = i2c_read_byte(&bytes[i], i + 1u < count);
    }
    i2c_stop();
    return ok;
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

static bool rtc_get_time(ClockTime *out, bool *stopped)
{
    uint8_t bytes[3];
    ClockTime time;
    *stopped = false;
    if (!rtc_read_block(0, bytes, sizeof bytes)) {
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

static bool rtc_set_time(uint16_t minutes, uint8_t seconds)
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

static bool schedule_equal(Schedule a, Schedule b)
{
    return a.start == b.start && a.end == b.end;
}

static bool rtc_load_schedule(Schedule *schedule)
{
    uint8_t bytes[6];
    if (!rtc_read_block(SCHEDULE_REG, bytes, sizeof bytes)
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

static bool rtc_save_schedule(Schedule schedule)
{
    uint8_t bytes[6] = {
        SCHEDULE_MARKER, (uint8_t)schedule.start, (uint8_t)(schedule.start >> 8),
        (uint8_t)schedule.end, (uint8_t)(schedule.end >> 8), 0
    };
    bytes[5] = schedule_check(bytes);
    Schedule stored;
    return i2c_write_block(DS1307_ADDRESS, SCHEDULE_REG, bytes, sizeof bytes)
        && rtc_load_schedule(&stored) && schedule_equal(stored, schedule);
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

static void task_sensors(void *argument)
{
    (void)argument;
    SensorData sensors = { 0 };
    const TickType_t dht_period = pdMS_TO_TICKS(2100);
    TickType_t wake = xTaskGetTickCount();
    TickType_t dht_at = wake - dht_period;
    TickType_t last_good_rtc = wake;
    for (;;) {
        SaveRequest request;
        bool have_request = xQueueReceive(save_queue, &request, 0) == pdTRUE;
        bool bus_available = xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(45)) == pdTRUE;
        if (have_request) {
            sensors.save_id = request.id;
            sensors.clock_saved = bus_available && request.clock_changed
                               && rtc_set_time(request.clock_minutes, request.clock_seconds);
            sensors.save_ok = bus_available
                            && (!request.clock_changed || sensors.clock_saved)
                            && (!request.schedule_changed || rtc_save_schedule(request.schedule));
        }
        ClockTime current_time;
        bool stopped = false;
        bool read_ok = bus_available && rtc_get_time(&current_time, &stopped);
        if (read_ok) {
            sensors.clock = current_time;
            sensors.rtc_ok = true;
            sensors.rtc_stopped = false;
            last_good_rtc = xTaskGetTickCount();
        } else if (stopped) {
            sensors.rtc_ok = false;
            sensors.rtc_stopped = true;
        } else if (!sensors.rtc_ok
                   || (TickType_t)(xTaskGetTickCount() - last_good_rtc) >= pdMS_TO_TICKS(1250)) {
            sensors.rtc_ok = false;
            sensors.rtc_stopped = false;
        }
        if (bus_available) {
            xSemaphoreGive(i2c_mutex);
        }

        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - dht_at) >= dht_period) {
            dht_at = now;
            sensors.dht_ok = dht11_read(&sensors);
        }
        xQueueOverwrite(sensor_queue, &sensors);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(250));
    }
}

static KeyEvent button_update(Button *button, bool pressed, TickType_t now)
{
    if (pressed != button->raw) {
        button->raw = pressed;
        button->changed_at = now;
    }
    if (button->stable != button->raw
        && (TickType_t)(now - button->changed_at) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        button->stable = button->raw;
        if (button->stable) {
            button->pressed_at = now;
            button->held = false;
            return KEY_PRESS;
        }
        return button->held ? KEY_NONE : KEY_TAP;
    }
    if (button->stable && button->raw && !button->held
        && (TickType_t)(now - button->pressed_at) >= pdMS_TO_TICKS(MODE_HOLD_MS)) {
        button->held = true;
        return KEY_HOLD;
    }
    return KEY_NONE;
}

static uint16_t clock_minutes(ClockTime time)
{
    return (uint16_t)(time.hour * 60u + time.minute);
}

static bool schedule_active(Schedule schedule, uint16_t time)
{
    if (schedule.start < schedule.end) {
        return time >= schedule.start && time < schedule.end;
    }
    if (schedule.start > schedule.end) {
        return time >= schedule.start || time < schedule.end;
    }
    return false;
}

static uint16_t adjust_time(uint16_t time, bool adjust_hour, int change)
{
    int hour = time / 60u;
    int minute = time % 60u;
    if (adjust_hour) {
        hour = (hour + 24 + change) % 24;
    } else {
        minute = (minute + 60 + change) % 60;
    }
    return (uint16_t)(hour * 60 + minute);
}

static bool editing_hour(MenuField field)
{
    return field == RTC_HOUR || field == START_HOUR || field == END_HOUR;
}

static void edit_menu(DisplayData *state, KeyEvent mode, int change)
{
    if (state->status == MENU_SAVING) {
        return;
    }
    if (mode == KEY_HOLD) {
        if (state->draft.schedule.start == state->draft.schedule.end) {
            state->status = MENU_EQUAL_TIMES;
            return;
        }
        state->draft.schedule_changed = !schedule_equal(state->schedule, state->draft.schedule);
        ++state->draft.id;
        xQueueOverwrite(save_queue, &state->draft);
        state->status = MENU_SAVING;
    } else if (mode == KEY_TAP) {
        state->field = state->field == END_MINUTE ? RTC_HOUR : (MenuField)(state->field + 1);
    } else if (change != 0) {
        bool hour = editing_hour(state->field);
        if (state->field == RTC_SECOND) {
            state->draft.clock_seconds = (uint8_t)((state->draft.clock_seconds + 60 + change) % 60);
            state->draft.clock_changed = true;
        } else if (state->field <= RTC_MINUTE) {
            state->draft.clock_minutes = adjust_time(state->draft.clock_minutes, hour, change);
            state->draft.clock_changed = true;
        } else if (state->field <= START_MINUTE) {
            state->draft.schedule.start = adjust_time(state->draft.schedule.start, hour, change);
        } else {
            state->draft.schedule.end = adjust_time(state->draft.schedule.end, hour, change);
        }
        state->status = MENU_READY;
    }
}

static void task_control(void *argument)
{
    (void)argument;
    DisplayData state = { .schedule = boot_schedule, .automatic = true };
    Button mode = { 0 }, up = { 0 }, down = { 0 };
    bool manual_open = false;
    TickType_t wet_at = 0;
    TickType_t wake = xTaskGetTickCount();
    TickType_t last_motion_tick = wake;
    TickType_t position = 0; /* Uoc luong vi tri: khoi dong o vi tri DONG. */
    TickType_t travel = pdMS_TO_TICKS(SERVO_TRAVEL_MS);
    int motor_direction = 0;
    if (travel == 0) {
        travel = 1;
    }

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t elapsed = now - last_motion_tick;
        last_motion_tick = now;
        if (motor_direction > 0) {
            position += elapsed < travel - position ? elapsed : travel - position;
        } else if (motor_direction < 0) {
            position -= elapsed < position ? elapsed : position;
        }
        (void)xQueuePeek(sensor_queue, &state.sensors, 0);

        bool wet = (GPIOA_IDR & RAIN_PIN) == 0u;
        if (wet) {
            wet_at = now;
        }
        state.raining = wet || (state.raining
                      && (TickType_t)(now - wet_at) < pdMS_TO_TICKS(RAIN_RELEASE_MS));

        if (state.status == MENU_SAVING && state.sensors.save_id == state.draft.id) {
            if (state.sensors.clock_saved) {
                state.draft.clock_changed = false;
            }
            if (state.sensors.save_ok) {
                state.schedule = state.draft.schedule;
                state.field = HOME;
                state.status = MENU_READY;
            } else {
                state.status = MENU_SAVE_ERROR;
            }
        }

        uint32_t pins = GPIOB_IDR;
        KeyEvent mode_event = button_update(&mode, (pins & MODE_PIN) == 0u, now);
        bool up_pressed = button_update(&up, (pins & UP_PIN) == 0u, now) == KEY_PRESS;
        bool down_pressed = button_update(&down, (pins & DOWN_PIN) == 0u, now) == KEY_PRESS;
        if (state.field != HOME) {
            edit_menu(&state, mode_event, (int)up_pressed - (int)down_pressed);
        } else if (mode_event == KEY_HOLD) {
            state.draft.schedule = state.schedule;
            state.draft.clock_minutes = state.sensors.rtc_ok ? clock_minutes(state.sensors.clock) : 0u;
            state.draft.clock_seconds = state.sensors.rtc_ok ? state.sensors.clock.second : 0u;
            state.draft.clock_changed = !state.sensors.rtc_ok;
            state.field = RTC_HOUR;
        } else if (mode_event == KEY_TAP) {
            state.automatic = !state.automatic;
            manual_open = state.rack_open;
        } else if (!state.automatic) {
            if (down_pressed) {
                manual_open = false;
            } else if (up_pressed && !state.raining) {
                manual_open = true;
            }
        }

        if (state.raining) {
            manual_open = false;
        }
        bool too_humid = state.sensors.dht_ok && state.sensors.humidity > HIGH_HUMIDITY;
        bool auto_open = state.sensors.rtc_ok && !too_humid
                      && schedule_active(state.schedule, clock_minutes(state.sensors.clock));
        bool desired_open = !state.raining && (state.automatic ? auto_open : manual_open);
        motor_direction = desired_open ? (position < travel ? 1 : 0)
                                       : (position > 0 ? -1 : 0);
        TIM2_CCR1 = motor_direction > 0 ? SERVO_OPEN_US
                  : motor_direction < 0 ? SERVO_CLOSE_US : SERVO_STOP_US;
        state.rack_open = position == travel;
        state.rack_moving = motor_direction != 0;
        state.moving_open = motor_direction > 0;

        xQueueOverwrite(display_queue, &state);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

/* OLED SSD1306, ky tu 5x7, cac dong theo page. */
static const char font_characters[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:-/%>=";
static const uint8_t font[][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
    {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
    {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E},
    {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
    {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
    {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
    {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
    {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
    {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
    {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
    {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
    {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43},
    {0x00,0x36,0x36,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08},
    {0x20,0x10,0x08,0x04,0x02}, {0x23,0x13,0x08,0x64,0x62},
    {0x00,0x41,0x22,0x14,0x08}, {0x14,0x14,0x14,0x14,0x14},
};
_Static_assert(sizeof font / sizeof font[0] == sizeof font_characters - 1u,
               "Font size must match the character list");

static bool oled_init(void)
{
    const uint8_t commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    return i2c_write_block(OLED_ADDRESS, 0x00, commands, sizeof commands);
}

static bool oled_write_line(uint8_t page, const char *text)
{
    uint8_t pixels[DISPLAY_WIDTH] = { 0 };
    for (size_t i = 0; i < DISPLAY_COLUMNS && text[i] != '\0'; ++i) {
        size_t glyph = 0;
        while (font_characters[glyph] != '\0' && font_characters[glyph] != text[i]) {
            ++glyph;
        }
        if (font_characters[glyph] != '\0') {
            memcpy(&pixels[6u * i], font[glyph], sizeof font[glyph]);
        }
    }
    const uint8_t cursor[] = { (uint8_t)(0xB0u + page), 0x00, 0x10 };
    return i2c_write_block(OLED_ADDRESS, 0x00, cursor, sizeof cursor)
        && i2c_write_block(OLED_ADDRESS, 0x40, pixels, sizeof pixels);
}

static void line_text(char *line, const char *text)
{
    size_t i = 0;
    for (; i < DISPLAY_COLUMNS && text[i] != '\0'; ++i) {
        line[i] = text[i];
    }
    line[i] = '\0';
}

static bool same_text(const char *a, const char *b)
{
    while (*a == *b && *a != '\0') {
        ++a;
        ++b;
    }
    return *a == *b;
}

static void put_two(char *text, unsigned int value)
{
    text[0] = (char)('0' + value / 10u);
    text[1] = (char)('0' + value % 10u);
}

static void put_hhmm(char *text, uint16_t minutes)
{
    put_two(text, minutes / 60u);
    text[2] = ':';
    put_two(text + 3, minutes % 60u);
}

static void display_lines(const DisplayData *state, char lines[DISPLAY_PAGES][DISPLAY_COLUMNS + 1u])
{
    if (state->field == HOME) {
        line_text(lines[0], state->automatic ? "RTOS AUTO" : "RTOS MANUAL");
        if (state->sensors.rtc_ok) {
            line_text(lines[1], "RTC 00:00:00");
            put_hhmm(lines[1] + 4, clock_minutes(state->sensors.clock));
            put_two(lines[1] + 10, state->sensors.clock.second);
        } else {
            line_text(lines[1], state->sensors.rtc_stopped
                       ? "RTC STOP SET CLOCK" : "RTC ERR CHECK I2C");
        }
        line_text(lines[2], "PHOI 00:00-00:00");
        put_hhmm(lines[2] + 5, state->schedule.start);
        put_hhmm(lines[2] + 11, state->schedule.end);
        if (state->sensors.dht_ok) {
            line_text(lines[3], "T:00C H:000%");
            put_two(lines[3] + 2, state->sensors.temperature);
            lines[3][8] = state->sensors.humidity == 100u ? '1' : ' ';
            put_two(lines[3] + 9, state->sensors.humidity % 100u);
        } else {
            line_text(lines[3], "DHT11 ERR");
        }
        line_text(lines[4], state->raining ? "RAIN: YES" : "RAIN: NO");
        line_text(lines[5], state->rack_moving
                  ? (state->moving_open ? "RACK: OPENING" : "RACK: CLOSING")
                  : (state->rack_open ? "RACK: OPEN" : "RACK: CLOSE"));
        line_text(lines[7], "HOLD MODE TO SET");
        return;
    }

    line_text(lines[0], "CAI DAT GIO");
    line_text(lines[1], " RTC   00:00:00");
    line_text(lines[2], " START 00:00");
    line_text(lines[3], " END   00:00");
    put_hhmm(lines[1] + 7, state->draft.clock_minutes);
    put_two(lines[1] + 13, state->draft.clock_seconds);
    put_hhmm(lines[2] + 7, state->draft.schedule.start);
    put_hhmm(lines[3] + 7, state->draft.schedule.end);
    lines[state->field <= RTC_SECOND ? 1u
          : state->field <= START_MINUTE ? 2u : 3u][0] = '>';
    line_text(lines[4], state->field == RTC_SECOND ? "EDIT SECOND"
              : editing_hour(state->field) ? "EDIT HOUR" : "EDIT MINUTE");
    if (state->status == MENU_SAVING) {
        line_text(lines[6], "DANG LUU");
    } else if (state->status == MENU_EQUAL_TIMES) {
        line_text(lines[6], "START = END");
    } else if (state->status == MENU_SAVE_ERROR) {
        line_text(lines[6], "RTC SAVE ERR");
    }
    line_text(lines[7], "MODE:NEXT HOLD:SAVE");
}

static void task_display(void *argument)
{
    (void)argument;
    static char previous[DISPLAY_PAGES][DISPLAY_COLUMNS + 1u];
    uint8_t drawn_pages = 0;
    bool ready = false;
    vTaskDelay(pdMS_TO_TICKS(120));

    for (;;) {
        if (!ready && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(40)) == pdTRUE) {
            ready = oled_init();
            xSemaphoreGive(i2c_mutex);
            drawn_pages = 0;
        }

        DisplayData state;
        if (ready && xQueuePeek(display_queue, &state, 0) == pdTRUE) {
            char lines[DISPLAY_PAGES][DISPLAY_COLUMNS + 1u] = { { 0 } };
            display_lines(&state, lines);
            for (uint8_t page = 0; page < DISPLAY_PAGES; ++page) {
                uint8_t bit = (uint8_t)BIT(page);
                if ((drawn_pages & bit) != 0u && same_text(lines[page], previous[page])) {
                    continue;
                }
                if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(40)) != pdTRUE) {
                    break;
                }
                ready = oled_write_line(page, lines[page]);
                xSemaphoreGive(i2c_mutex);
                if (!ready) {
                    break;
                }
                line_text(previous[page], lines[page]);
                drawn_pages |= bit;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

int main(void)
{
    hardware_init();
    delay_us(10000);
    (void)rtc_load_schedule(&boot_schedule);

    i2c_mutex = xSemaphoreCreateMutex();
    sensor_queue = xQueueCreate(1, sizeof(SensorData));
    display_queue = xQueueCreate(1, sizeof(DisplayData));
    save_queue = xQueueCreate(1, sizeof(SaveRequest));
    if (i2c_mutex != NULL && sensor_queue != NULL && display_queue != NULL && save_queue != NULL
        && xTaskCreate(task_control, "Control", 256, NULL, 3, NULL) == pdPASS
        && xTaskCreate(task_sensors, "Sensors", 256, NULL, 2, NULL) == pdPASS
        && xTaskCreate(task_display, "Display", 256, NULL, 1, NULL) == pdPASS) {
        vTaskStartScheduler();
    }

    for (;;) {
    }
}

