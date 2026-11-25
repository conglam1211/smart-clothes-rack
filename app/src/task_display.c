/* Display: render home and clock/schedule edit menu. */
#include "app_ipc.h"
#include "app_cfg.h"
#include "hal_devices.h"
#include "task_display.h"
#include "task.h"

#define DISPLAY_WIDTH 128u
#define DISPLAY_PAGES 8u
#define DISPLAY_COLUMNS 21u

static uint16_t clock_minutes(ClockTime time)
{
    return (uint16_t)(time.hour * 60u + time.minute);
}

static bool editing_hour(MenuField field)
{
    return field == RTC_HOUR || field == START_HOUR || field == END_HOUR;
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
        if (!ready) {
            ready = hal_oled_init();
            drawn_pages = 0;
        }

        DisplayData state;
        if (ready && xQueuePeek(display_queue, &state, 0) == pdTRUE) {
            char lines[DISPLAY_PAGES][DISPLAY_COLUMNS + 1u] = { { 0 } };
            display_lines(&state, lines);
            for (uint8_t page = 0; page < DISPLAY_PAGES; ++page) {
                uint8_t bit = (uint8_t)(1u << page);
                if ((drawn_pages & bit) != 0u && same_text(lines[page], previous[page])) {
                    continue;
                }
                ready = hal_oled_write_line(page, lines[page]);
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

BaseType_t task_display_create(void)
{
    return xTaskCreate(task_display, "Display",
                       TASK_DISPLAY_STACK_WORDS, NULL,
                       TASK_DISPLAY_PRIORITY, NULL);
}
