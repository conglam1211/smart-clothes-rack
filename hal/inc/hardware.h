#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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

#define SERVO_STOP_US     1500u  /* SG90 360 do: xung trung tinh, dung quay. */

static inline uint16_t micros(void)
{
    return (uint16_t)TIM3_CNT;
}

void hardware_init(void);
void delay_us(uint16_t duration);
bool wait_level(volatile const uint32_t *input, uint32_t pin, bool high, uint16_t timeout_us);
void gpio_configure(volatile uint32_t *config, unsigned int pin, uint32_t mode);

/* Task goi giu i2c_mutex trong suot giao dich. */
bool i2c_write_block(uint8_t address, uint8_t selector, const uint8_t *bytes, size_t count);
bool i2c_read_block(uint8_t address, uint8_t selector, uint8_t *bytes, size_t count);

#endif
