#ifndef HAL_DEVICES_H
#define HAL_DEVICES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HAL_BUTTON_MODE,
    HAL_BUTTON_UP,
    HAL_BUTTON_DOWN
} HalButton;

/* Khoi tao GPIO va timer cua Blue Pill. */
void hal_devices_init(void);

/* DS1307: gio 24h; start/end tinh bang phut tu 00:00. */
bool hal_rtc_get_time(uint8_t *hour, uint8_t *minute,
                      uint8_t *second, bool *stopped);
bool hal_rtc_set_time(uint8_t hour, uint8_t minute, uint8_t second);
bool hal_rtc_load_schedule(uint16_t *start, uint16_t *end);
bool hal_rtc_save_schedule(uint16_t start, uint16_t end);

/* DHT11, cam bien mua va ba nut bam. */
bool hal_dht11_read(uint8_t *temperature, uint8_t *humidity);
bool hal_rain_is_wet(void);
bool hal_button_is_pressed(HalButton button);

/* SG90 360 do: 1500 us dung; 1700/1300 us quay hai chieu. */
void hal_servo_set_pulse_us(uint16_t pulse_us);

/* OLED SSD1306: page tu 0 den 7. */
bool hal_oled_init(void);
bool hal_oled_write_line(uint8_t page, const char *text);

#endif
