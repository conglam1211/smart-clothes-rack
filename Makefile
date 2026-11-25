.RECIPEPREFIX := >
RTOS_DIR := middlewares/FreeRTOS
CC := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
CFLAGS := -mcpu=cortex-m3 -mthumb -O1 -g -ffreestanding -nostdlib \
          -I. -Iapp/inc -Ihal/inc \
          -I$(RTOS_DIR)/include \
          -I$(RTOS_DIR)/portable/GCC/ARM_CM3
SOURCES := startup.c \
           hal/src/hardware.c hal/src/hal_sensors.c \
           hal/src/hal_control.c hal/src/hal_oled.c \
           app/src/main.c app/src/app_ipc.c \
           app/src/task_sensors.c app/src/task_control.c app/src/task_display.c \
           $(RTOS_DIR)/tasks.c $(RTOS_DIR)/queue.c $(RTOS_DIR)/list.c \
           $(RTOS_DIR)/portable/GCC/ARM_CM3/port.c \
           $(RTOS_DIR)/portable/MemMang/heap_4.c
HEADERS := $(wildcard app/inc/*.h hal/inc/*.h)
.PHONY: all flash clean
all: build/rtos_test.bin
build:
>mkdir -p build
build/rtos_test.elf: $(SOURCES) $(HEADERS) stm32f103.ld | build
>$(CC) $(CFLAGS) -T stm32f103.ld $(SOURCES) -lgcc -o $@
build/rtos_test.bin: build/rtos_test.elf
>$(OBJCOPY) -O binary $< $@
flash: all
>st-flash write build/rtos_test.bin 0x08000000
clean:
>rm -rf build
