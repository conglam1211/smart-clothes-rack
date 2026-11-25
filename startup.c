#include <stdint.h>

extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern int main(void);

extern void SVC_Handler(void);
extern void PendSV_Handler(void);
extern void SysTick_Handler(void);

#define SRAM_START  0x20000000U
#define SRAM_SIZE   (20U * 1024U)
#define SRAM_END    ((SRAM_START) + (SRAM_SIZE))
#define STACK_START SRAM_END

void Default_Handler(void) {
    while (1);
}

void Reset_Handler(void) {
    uint32_t *pSrc  = &_etext;
    uint32_t *pDest = &_sdata;
    while (pDest < &_edata) {
        *pDest++ = *pSrc++;
    }

    pDest = &_sbss;
    while (pDest < &_ebss) {
        *pDest++ = 0;
    }

    main();
    while (1);
}

__attribute__((section(".isr_vector")))
void (* const VectorTable[])(void) = {
    (void (*)(void))STACK_START, /* 0: Stack Pointer */
    Reset_Handler,               /* 1: Reset */
    Default_Handler,             /* 2: NMI */
    Default_Handler,             /* 3: HardFault */
    Default_Handler,             /* 4: MemManage */
    Default_Handler,             /* 5: BusFault */
    Default_Handler,             /* 6: UsageFault */
    0, 0, 0, 0,                  /* 7-10: Reserved */
    SVC_Handler,                 /* 11: SVCall (FreeRTOS) */
    Default_Handler,             /* 12: DebugMon */
    0,                           /* 13: Reserved */
    PendSV_Handler,              /* 14: PendSV (FreeRTOS) */
    SysTick_Handler              /* 15: SysTick (FreeRTOS) */
};
