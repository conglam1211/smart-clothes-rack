/* PA5: rain DO active low; PB12/13/14: buttons; PA0: TIM2 servo. */
#include "hardware.h"
#include "hal_devices.h"

bool hal_rain_is_wet(void)
{
    return (GPIOA_IDR & RAIN_PIN) == 0u;
}

bool hal_button_is_pressed(HalButton button)
{
    uint32_t mask;
    switch (button) {
    case HAL_BUTTON_MODE: mask = MODE_PIN; break;
    case HAL_BUTTON_UP:   mask = UP_PIN; break;
    case HAL_BUTTON_DOWN: mask = DOWN_PIN; break;
    default: return false;
    }
    return (GPIOB_IDR & mask) == 0u;
}

void hal_servo_set_pulse_us(uint16_t pulse_us)
{
    TIM2_CCR1 = pulse_us;
}
