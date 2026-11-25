#ifndef OLED_H
#define OLED_H
#include <stdint.h>

void OLED_Init(void);
void OLED_Clear(void);
void OLED_SetCursor(uint8_t x, uint8_t y);
void OLED_PrintChar(char c);
void OLED_PrintString(char *str);

#endif
