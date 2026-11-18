#include <stdint.h>

extern uint32_t _etext;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;
extern void main(void);

#define SRAM_START  0x20000000U
#define SRAM_SIZE   (20U * 1024U)
#define SRAM_END    ((SRAM_START) + (SRAM_SIZE))
#define STACK_START SRAM_END

void Reset_Handler(void) {
    uint32_t *pSrc, *pDest;

    // Copy .data section from flash to SRAM
    pSrc  = &_etext;
    pDest = &_sdata;
    while (pDest < &_edata) {
        *pDest++ = *pSrc++;
    }

    // Init .bss section to zero in SRAM
    pDest = &_sbss;
    while (pDest < &_ebss) {
        *pDest++ = 0;
    }

    main();
    while (1);
}

__attribute__((section(".isr_vector")))
const void (*VectorTable[])(void) = {
    (const void (*)(void))STACK_START,
    Reset_Handler
};
