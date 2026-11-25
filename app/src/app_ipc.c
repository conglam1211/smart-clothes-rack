#include "app_ipc.h"
QueueHandle_t sensor_queue, display_queue, save_queue;
SemaphoreHandle_t i2c_mutex;
Schedule boot_schedule = { 7u * 60u, 17u * 60u + 30u };

bool app_ipc_init(void)
{
    i2c_mutex = xSemaphoreCreateMutex();
    sensor_queue = xQueueCreate(1, sizeof(SensorData));
    display_queue = xQueueCreate(1, sizeof(DisplayData));
    save_queue = xQueueCreate(1, sizeof(SaveRequest));
    return i2c_mutex != NULL && sensor_queue != NULL &&
           display_queue != NULL && save_queue != NULL;
}
