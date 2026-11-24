/* Khoi tao Blue Pill, timer va I2C bang thanh ghi. */

#include "hardware.h"
#include "hal_devices.h"

void delay_us(uint16_t duration)
{
    uint16_t start = micros();
    while ((uint16_t)(micros() - start) < duration) {
    }
}

bool wait_level(volatile const uint32_t *input, uint32_t pin,
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

void gpio_configure(volatile uint32_t *config, unsigned int pin, uint32_t mode)
{
    unsigned int shift = (pin % 8u) * 4u;
    *config = (*config & ~(0xFu << shift)) | (mode << shift);
}

void hardware_init(void)
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

bool i2c_write_block(uint8_t address, uint8_t selector,
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

bool i2c_read_block(uint8_t address, uint8_t selector, uint8_t *bytes, size_t count)
{
    bool ok = i2c_start() && i2c_write_byte((uint8_t)(address << 1))
                          && i2c_write_byte(selector) && i2c_start()
                          && i2c_write_byte((uint8_t)((address << 1) | 1u));
    for (size_t i = 0; ok && i < count; ++i) {
        ok = i2c_read_byte(&bytes[i], i + 1u < count);
    }
    i2c_stop();
    return ok;
}

void hal_devices_init(void)
{
    hardware_init();
}
