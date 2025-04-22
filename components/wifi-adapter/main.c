
#include "globalvars.h"
#include "main.h"
#include "osd.h"
#include "vga.h"
#include "capture.h"
#include "nvs.h"
#include "wlan.h"

// Hauptprogramm
void IRAM_ATTR app_main(void)
{
	setup_flash();
	restore_settings();

	alloc_vga_buffer();
	setup_vga_buffer();
	setup_vga_mode();

	bmp_img = heap_caps_malloc(1024 * ABG_YRes, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
    bmp_line_length = heap_caps_malloc(2 * ABG_YRes, MALLOC_CAP_INTERNAL);

    setup_abg();
	xTaskCreatePinnedToCore(osd_task,"osd_task",8000,NULL,0,NULL,0);
	setup_wlan(wlan_mode);
}

