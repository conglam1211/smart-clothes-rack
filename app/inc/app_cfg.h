#ifndef APP_CFG_H
#define APP_CFG_H

/* Stack tinh theo word, giong tham so xTaskCreate hien tai. */
#define TASK_CONTROL_STACK_WORDS 256U
#define TASK_SENSOR_STACK_WORDS  256U
#define TASK_DISPLAY_STACK_WORDS 256U

#define TASK_CONTROL_PRIORITY 3U
#define TASK_SENSOR_PRIORITY  2U
#define TASK_DISPLAY_PRIORITY 1U

#endif
