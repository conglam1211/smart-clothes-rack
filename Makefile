.RECIPEPREFIX := >

RTOS_DIR := middlewares/FreeRTOS
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy

CFLAGS := -mcpu=cortex-m3 -mthumb -O1 -g -nostdlib \
          -I. -Iapp/inc \
          -I$(RTOS_DIR)/include \
          -I$(RTOS_DIR)/portable/GCC/ARM_CM3

SOURCES := startup.c \
           app/src/main.c \
           $(RTOS_DIR)/tasks.c \
           $(RTOS_DIR)/queue.c \
           $(RTOS_DIR)/list.c \
           $(RTOS_DIR)/portable/GCC/ARM_CM3/port.c \
           $(RTOS_DIR)/portable/MemMang/heap_4.c

.PHONY: all flash clean

all: build/rtos_test.bin

build:
>mkdir -p build

build/rtos_test.elf: $(SOURCES) stm32f103.ld app/inc/FreeRTOSConfig.h | build
>$(CC) $(CFLAGS) -T stm32f103.ld $(SOURCES) -o $@

build/rtos_test.bin: build/rtos_test.elf
>$(OBJCOPY) -O binary $< $@

flash: all
>st-flash write build/rtos_test.bin 0x08000000

clean:
>rm -rf build
