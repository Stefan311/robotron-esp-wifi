
#include <limits.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "globalvars.h"

// Statische Werte vorinitialisiert
const struct SYSSTATIC _STATIC_SYS_VALS[] = {
	{ 
		.name = "A7100",
		.colors = {0, 0b00000100, 0b00001000, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 8,
		.xres = 640,
		.yres = 400,
		.default_pixel_abstand = 8950,
		.default_start_line = 32,
		.default_pixel_per_line = 73600,
	},
	{ 
		.name = "PC1715",
		.colors = {0b00001000, 0b00000100, 0, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 4,
		.xres = 640,
		.yres = 299, /* 24 Zeilen (288 + Statuszeile) 299 im echten Leben*/
		.default_pixel_abstand = 15532 /* schwankt bis auf 15588 */,
		.default_start_line = 24,
		.default_pixel_per_line = 86410,
	},
	{ 
		.name = "EC1834",
		.colors = {0, 0b00000100, 0b00001000, 0b00001100}, // 0b--rrggbb
		.bits_per_sample = 8,
		.xres = 720,
		.yres = 350,
		.default_pixel_abstand = 10717,
		.default_start_line = 18,
		.default_pixel_per_line = 86400,
	},	
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
volatile uint32_t ABG_Last_Scan_Line = 0;
uint16_t ABG_XRes = 0;
uint16_t ABG_YRes = 0;
uint8_t ABG_Bits_per_sample = 0;
uint16_t ABG_Total_Scanlines = 0;
uint16_t ABG_Total_Scanl_Repeat = 0;
uint16_t ABG_Last_Scanl_Repeat = 0;

uint8_t* PIXEL_STEP_LIST;
uint8_t* OSD_BUF;
volatile uint32_t bsyn_clock_diff = 0;
volatile uint32_t bsyn_clock_last = 0;
volatile uint32_t bsyn_clock_frame = 0;
volatile uint32_t BSYNC_SAMPLE_ABSTAND = 0;
volatile bool osd_aktiv = false;
volatile bool osd_bmp_index = false;
uint8_t* osd_bmp_img;
uint16_t* osd_bmp_length;

uint8_t wps_mode = 0;
char wlan_ssid[64] = "";
char wlan_passwd[64] = "";
uint16_t* bmp_line_length;
uint8_t* bmp_img;
uint32_t bmp_palette[10] = { 0x00000000, 0x00005500, 0x0000aa00, 0x0000ff00 , 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff };
