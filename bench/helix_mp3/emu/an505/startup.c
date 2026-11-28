/* minimal Cortex-M33 start-up for QEMU mps2-an505, semihosting stdio (newlib rdimon) */
#include <stdint.h>
#include <string.h>

extern uint32_t __bss_start__[], __bss_end__[], __stack_top[];
extern int main(void);
extern void initialise_monitor_handles(void);
extern void exit(int);

void Reset_Handler(void)
{
    *(volatile uint32_t*) 0xe000ed88 |= 0xfu << 20;  // CPACR: enable CP10/CP11 (the softfp ABI may use the FPU)
    __asm__ volatile ("dsb\n isb");
    memset(__bss_start__, 0, (size_t) ((uintptr_t) __bss_end__ - (uintptr_t) __bss_start__));
    initialise_monitor_handles();
    exit(main());
}

static void Fault_Handler(void)
{
    while (1) {}
}

__attribute__((section(".vectors"), used)) static const void* vectors[16] = {
    __stack_top, Reset_Handler, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler, Fault_Handler,
};
