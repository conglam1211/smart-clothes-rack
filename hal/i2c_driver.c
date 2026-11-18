#include "i2c_driver.h"

// Định nghĩa thanh ghi tay (Bare-metal) cho GPIOB
#define RCC_APB2ENR   (*((volatile uint32_t*)0x40021018))
#define GPIOB_CRL     (*((volatile uint32_t*)0x40010C00))
#define GPIOB_IDR     (*((volatile uint32_t*)0x40010C08))
#define GPIOB_BSRR    (*((volatile uint32_t*)0x40010C10))

// Macro ép xung I2C
#define SCL_HIGH()    (GPIOB_BSRR = (1 << 6))
#define SCL_LOW()     (GPIOB_BSRR = (1 << 22))
#define SDA_HIGH()    (GPIOB_BSRR = (1 << 7))
#define SDA_LOW()     (GPIOB_BSRR = (1 << 23))
#define SDA_READ()    ((GPIOB_IDR & (1 << 7)) ? 1 : 0)

// Hàm delay tạo xung (Tốc độ ~100kHz)
static void I2C_Delay(void) {
    for(volatile int i = 0; i < 20; i++);
}

void I2C_Init(void) {
    RCC_APB2ENR |= (1 << 3); // Cấp xung nhịp cho GPIOB
    // Cấu hình PB6 (SCL) và PB7 (SDA) là Output Open-Drain (MODE=11, CNF=01)
    GPIOB_CRL &= ~(0xFF000000); 
    GPIOB_CRL |=  (0x77000000); 
    SCL_HIGH();
    SDA_HIGH();
}

void I2C_Start(void) {
    SDA_HIGH(); I2C_Delay();
    SCL_HIGH(); I2C_Delay();
    SDA_LOW();  I2C_Delay();
    SCL_LOW();  I2C_Delay();
}

void I2C_Stop(void) {
    SDA_LOW();  I2C_Delay();
    SCL_HIGH(); I2C_Delay();
    SDA_HIGH(); I2C_Delay();
}

void I2C_WriteByte(uint8_t data) {
    for (uint8_t i = 0; i < 8; i++) {
        if (data & 0x80) SDA_HIGH();
        else SDA_LOW();
        data <<= 1;
        I2C_Delay();
        SCL_HIGH(); I2C_Delay();
        SCL_LOW();  I2C_Delay();
    }
    // Chờ ACK
    SDA_HIGH(); I2C_Delay();
    SCL_HIGH(); I2C_Delay();
    SCL_LOW();  I2C_Delay();
}

uint8_t I2C_ReadByte(uint8_t ack) {
    uint8_t data = 0;
    SDA_HIGH(); // Nhả chân SDA để thiết bị ngoại vi kéo
    for (uint8_t i = 0; i < 8; i++) {
        data <<= 1;
        SCL_HIGH(); I2C_Delay();
        if (SDA_READ()) data |= 0x01;
        SCL_LOW();  I2C_Delay();
    }
    // Gửi ACK (0) hoặc NACK (1)
    if (ack) SDA_LOW(); 
    else SDA_HIGH();
    I2C_Delay();
    SCL_HIGH(); I2C_Delay();
    SCL_LOW();  I2C_Delay();
    return data;
}
