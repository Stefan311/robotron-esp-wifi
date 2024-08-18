#pragma once

#include <limits.h>
#include "nvs_flash.h"
#include "nvs.h"

// Statische Struktur - Systemkonstanten
struct SYSSTATIC {
	char* name;
	uint8_t colors[4];
	uint8_t bits_per_sample; // 4 oder 8
	uint16_t xres; // 640 oder 720
	uint16_t yres;
	uint32_t default_pixel_abstand;
	uint32_t default_start_line;
	uint32_t default_pixel_per_line;
};

// Statische Werte vorinitialisiert
extern const struct SYSSTATIC _STATIC_SYS_VALS[];

// Bezeichner für NVS KEY - max 15 Zeichen!
#define _NVS_SETTING_MODE	"SMODE"
#define _NVS_SETTING_PIXEL_ABSTAND "SPIXABST(%d)"
#define _NVS_SETTING_START_LINE	"SSTARTLINE(%d)"
#define _NVS_SETTING_PIXEL_PER_LINE "SPIXPERLINE(%d)"
#define _NVS_SETTING_WPS_MODE	"WPSMODE"
#define _NVS_SETTING_SSID	"SSID"
#define _NVS_SETTING_PASSWD	"PASSWD"
#define _ABG_SAMPLE_BUFFER_COUNT 8

#define _SETTINGS_COUNT 3 // Anzahl unterstützter Computer = 3 (A7100,PC1715,EC1835)

// globale Variablen

// Aktives System
extern uint16_t ACTIVESYS;

extern nvs_handle_t sys_nvs_handle;

extern volatile uint32_t* ABG_DMALIST;
extern volatile uint32_t ABG_Scan_Line;
extern volatile double ABG_PIXEL_PER_LINE;
extern volatile double BSYNC_PIXEL_ABSTAND;
extern volatile uint32_t ABG_START_LINE;
extern volatile bool ABG_RUN;
extern volatile uint32_t ABG_Last_Scan_Line;
extern uint16_t ABG_XRes;
extern uint16_t ABG_YRes;
extern uint8_t ABG_Bits_per_sample;
extern uint16_t ABG_Total_Scanlines;
extern uint16_t ABG_Total_Scanl_Repeat;
extern uint16_t ABG_Last_Scanl_Repeat;

extern uint8_t* PIXEL_STEP_LIST;
extern uint8_t* OSD_BUF;
extern volatile uint32_t bsyn_clock_diff;
extern volatile uint32_t bsyn_clock_last;
extern volatile uint32_t bsyn_clock_frame;
extern volatile uint32_t BSYNC_SAMPLE_ABSTAND;
extern volatile bool osd_aktiv;
extern volatile bool osd_bmp_index;
extern uint8_t* osd_bmp_img;
extern uint16_t* osd_bmp_length;

extern uint8_t wps_mode;
extern char wlan_ssid[];
extern char wlan_passwd[];
extern uint16_t* bmp_line_length;
extern uint8_t* bmp_img;
extern uint32_t bmp_palette[10];

