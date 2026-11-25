/* Sensors: DS1307, DHT11, lich phoi va yeu cau luu. */
#include "app_ipc.h"
#include "app_cfg.h"
#include "hal_devices.h"
#include "task_sensors.h"
#include "task.h"

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
        if (have_request) {
            sensors.save_id = request.id;
            sensors.clock_saved = request.clock_changed
                               && hal_rtc_set_time((uint8_t)(request.clock_minutes / 60u),
                                                   (uint8_t)(request.clock_minutes % 60u),
                                                   request.clock_seconds);
            sensors.save_ok = (!request.clock_changed || sensors.clock_saved)
                            && (!request.schedule_changed
                                || hal_rtc_save_schedule(request.schedule.start,
                                                         request.schedule.end));
        }
        ClockTime current_time;
        bool stopped = false;
        bool read_ok = hal_rtc_get_time(&current_time.hour, &current_time.minute,
                                        &current_time.second, &stopped);
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

        TickType_t now = xTaskGetTickCount();
        if ((TickType_t)(now - dht_at) >= dht_period) {
            dht_at = now;
            sensors.dht_ok = hal_dht11_read(&sensors.temperature, &sensors.humidity);
        }
        xQueueOverwrite(sensor_queue, &sensors);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(250));
    }
}

BaseType_t task_sensors_create(void)
{
    return xTaskCreate(task_sensors, "Sensors",
                       TASK_SENSOR_STACK_WORDS, NULL,
                       TASK_SENSOR_PRIORITY, NULL);
}
