// Host stub (C) of pico_fatfs's tf_card.h as used by lib/file_menu
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define spi0 ((void*) 0)
#define CLK_SLOW_DEFAULT 0
#define MHZ 1000000
#define PIN_SPI0_MISO_DEFAULT 0
#define PIN_SPI0_CS_DEFAULT 0
#define PIN_SPI0_SCK_DEFAULT 0
#define PIN_SPI0_MOSI_DEFAULT 0
typedef struct { void* spi; uint32_t clk_slow; uint32_t clk_fast; uint32_t pin_miso; uint32_t pin_cs; uint32_t pin_sck; uint32_t pin_mosi; bool pullup; } pico_fatfs_spi_config_t;
static inline void pico_fatfs_set_config(pico_fatfs_spi_config_t* config) { (void) config; }
static inline void pico_fatfs_reboot_spi(void) {}

#ifdef __cplusplus
}
#endif
