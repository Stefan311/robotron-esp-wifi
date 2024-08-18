#include <driver/spi_master.h>
#include "esp_intr_alloc.h"
#include <xtensa/core-macros.h>
#include <freertos/task.h>
#include <rom/ets_sys.h>
#include <soc/periph_defs.h>

#include "globalvars.h"
#include "main.h"
#include "osd.h"
#include "vga.h"
#include "capture.h"
#include "pins.h"
#include "webserver.h"


// Hauptprogramm
void IRAM_ATTR app_main(void)
{
	setup_flash();
	// NVS Einstellungen laden, wenn vorhanden
	restore_settings();

//	heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
	setup_vga();
    bmp_img = heap_caps_malloc(1024 * ABG_YRes, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
    bmp_line_length = heap_caps_malloc(2 * ABG_YRes, MALLOC_CAP_INTERNAL);
    osd_bmp_img = heap_caps_malloc(1024 * 40, MALLOC_CAP_DEFAULT | MALLOC_CAP_SPIRAM);
    osd_bmp_length = heap_caps_malloc(2 * 40, MALLOC_CAP_INTERNAL);

    setup_abg();
	xTaskCreatePinnedToCore(osd_task,"osd_task",6000,NULL,0,NULL,0);
	setup_wlan();
}

