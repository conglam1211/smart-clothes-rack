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

#include "app_ipc.h"
#include "app_cfg.h"
#include "hal_devices.h"
#include "hardware.h"
#include "task_sensors.h"
#include "task_control.h"
#include "task_display.h"

extern Schedule boot_schedule;

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

int main(void)
{
    hal_devices_init();
    delay_us(10000);
    (void)hal_rtc_load_schedule(&boot_schedule.start, &boot_schedule.end);

    if (app_ipc_init()
        && task_control_create() == pdPASS
        && task_sensors_create() == pdPASS
        && task_display_create() == pdPASS) {
        vTaskStartScheduler();
    }

    for (;;) {
    }
}
