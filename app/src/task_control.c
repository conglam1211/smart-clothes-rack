/* Control: nut MODE/UP/DOWN, mua, lich phoi va SG90 360. */
#include "app_ipc.h"
#include "app_cfg.h"
#include "hal_devices.h"
#include "task_control.h"
#include "task.h"

extern Schedule boot_schedule; /* Defined by leader in app_ipc.c at integration. */

static bool schedule_equal(Schedule a, Schedule b)
{
    return a.start == b.start && a.end == b.end;
}

static uint16_t clock_minutes(ClockTime time)
{
    return (uint16_t)(time.hour * 60u + time.minute);
}

static bool editing_hour(MenuField field)
{
    return field == RTC_HOUR || field == START_HOUR || field == END_HOUR;
}

#define HIGH_HUMIDITY      85u
#define SERVO_OPEN_US      1700u
#define SERVO_CLOSE_US     1300u
#define SERVO_STOP_US      1500u
#define SERVO_TRAVEL_MS    700u
#define BUTTON_DEBOUNCE_MS 30u
#define MODE_HOLD_MS       1000u
#define RAIN_RELEASE_MS    250u

typedef enum { KEY_NONE, KEY_PRESS, KEY_TAP, KEY_HOLD } KeyEvent;

typedef struct {
    TickType_t changed_at, pressed_at;
    bool raw, stable, held;
} Button;

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

        bool wet = hal_rain_is_wet();
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

        KeyEvent mode_event = button_update(&mode, hal_button_is_pressed(HAL_BUTTON_MODE), now);
        bool up_pressed = button_update(&up, hal_button_is_pressed(HAL_BUTTON_UP), now) == KEY_PRESS;
        bool down_pressed = button_update(&down, hal_button_is_pressed(HAL_BUTTON_DOWN), now) == KEY_PRESS;
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
        hal_servo_set_pulse_us(motor_direction > 0 ? SERVO_OPEN_US
                             : motor_direction < 0 ? SERVO_CLOSE_US : SERVO_STOP_US);
        state.rack_open = position == travel;
        state.rack_moving = motor_direction != 0;
        state.moving_open = motor_direction > 0;

        xQueueOverwrite(display_queue, &state);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

BaseType_t task_control_create(void)
{
    return xTaskCreate(task_control, "Control",
                       TASK_CONTROL_STACK_WORDS, NULL,
                       TASK_CONTROL_PRIORITY, NULL);
}
