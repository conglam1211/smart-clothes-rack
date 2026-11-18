#include "rtc.h"
#include "../hal/i2c_driver.h"

#define DS1307_ADDR_WRITE 0xD0
#define DS1307_ADDR_READ  0xD1

// Đổi từ BCD sang Thập phân (Số đếm)
static uint8_t BCD2DEC(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

void RTC_Init(void) {
    I2C_Init();
    // Ghi bit CH (Clock Halt) = 0 để DS1307 bắt đầu đếm nhịp
    I2C_Start();
    I2C_WriteByte(DS1307_ADDR_WRITE);
    I2C_WriteByte(0x00); // Trỏ tới thanh ghi giây
    I2C_Start();
    I2C_WriteByte(DS1307_ADDR_READ);
    uint8_t sec = I2C_ReadByte(0);
    I2C_Stop();
    
    sec &= 0x7F; // Xóa bit CH
    I2C_Start();
    I2C_WriteByte(DS1307_ADDR_WRITE);
    I2C_WriteByte(0x00);
    I2C_WriteByte(sec);
    I2C_Stop();
}

void RTC_GetTime(RTC_Time_t *time) {
    I2C_Start();
    I2C_WriteByte(DS1307_ADDR_WRITE);
    I2C_WriteByte(0x00); // Bắt đầu đọc từ thanh ghi 0x00 (Giây)
    I2C_Stop();

    I2C_Start();
    I2C_WriteByte(DS1307_ADDR_READ);
    time->seconds = BCD2DEC(I2C_ReadByte(1) & 0x7F);
    time->minutes = BCD2DEC(I2C_ReadByte(1));
    time->hours   = BCD2DEC(I2C_ReadByte(1) & 0x3F); // Định dạng 24h
    time->day     = BCD2DEC(I2C_ReadByte(1));
    time->date    = BCD2DEC(I2C_ReadByte(1));
    time->month   = BCD2DEC(I2C_ReadByte(1));
    time->year    = BCD2DEC(I2C_ReadByte(0)); // Byte cuối dùng NACK (0)
    I2C_Stop();
}
