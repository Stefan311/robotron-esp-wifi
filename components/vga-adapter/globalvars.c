
#include <limits.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "globalvars.h"

// Statische Werte vorinitialisiert
const struct SYSSTATIC _STATIC_SYS_VALS[] = {
	{ 
		.name = "A7100 ",
		.colors = {0, 0b00000100, 0b00001000, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 8,
		.xres = 640,
		.yres = 400,
		.interleave_mask = 0,
		.default_pixel_abstand = 9010,
		.default_start_line = 29,
		.default_pixel_per_line = 73600,
	},
	{ 
		.name = "PC1715",
		.colors = {0b00001000, 0b00000100, 0, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 4,
		.xres = 640,
		.yres = 400,
		.interleave_mask = 0,
		.default_pixel_abstand = 15567 /* schwankt bis auf 15588 */,
		.default_start_line = 6,
		.default_pixel_per_line = 86410,
	},
	{ 
		.name = "EC1834",
		.colors = {0, 0b00000100, 0b00001000, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 8,
		.xres = 720,
		.yres = 350,
		.interleave_mask = 0,
		.default_pixel_abstand = 9010,     // Platzhalter, reale Parameter ermitteln!
		.default_start_line = 29,          // Platzhalter, reale Parameter ermitteln!
		.default_pixel_per_line = 73600,   // Platzhalter, reale Parameter ermitteln!
	}
};

// globale Variablen

// Aktives System
uint16_t ACTIVESYS = 0;
nvs_handle_t sys_nvs_handle;
volatile uint32_t* ABG_DMALIST;
volatile uint32_t ABG_Scan_Line = 0;
volatile double ABG_PIXEL_PER_LINE;
volatile double BSYNC_PIXEL_ABSTAND;
volatile uint32_t ABG_START_LINE;
volatile bool ABG_RUN = false;
uint32_t ABG_Interleave_Mask = 0;
uint32_t ABG_Interleave = 0;
uint16_t ABG_XRes = 0;
uint16_t ABG_YRes = 0;
uint8_t ABG_Bits_per_sample = 0;

int BSYNC_SUCHE_START = 0;
uint8_t* PIXEL_STEP_LIST;
uint8_t* VGA_BUF;
uint8_t* OSD_BUF;
volatile uint32_t bsyn_clock_diff = 0;
volatile uint32_t bsyn_clock_last = 0;
volatile uint32_t BSYNC_SAMPLE_ABSTAND = 0;

