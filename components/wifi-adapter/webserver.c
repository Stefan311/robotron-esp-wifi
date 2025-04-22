#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "globalvars.h"
#include "capture.h"
#include "main.h"
#include "pins.h"
#include "osd.h"

#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"
#include <driver/spi_master.h>
#include <xtensa_context.h>
#include <soc/gpio_reg.h>
#include <soc/gdma_reg.h>
#include <soc/spi_reg.h>
#include "esp_intr_alloc.h"
#include <rom/ets_sys.h>
#include <esp_http_server.h>
#include "esp_wps.h"

static httpd_handle_t web_server = NULL;

#define STREAM_CONTENT_BOUNDARY "638974789000000000000987654321"
void start_webserver();

// Webseiten-Handler BMP-Stream
static esp_err_t IRAM_ATTR mbmp_get_handler(httpd_req_t *req)
{
    httpd_send(req, "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: multipart/x-mixed-replace; boundary=" STREAM_CONTENT_BOUNDARY "\r\n",131);
    uint32_t bmp_header[14] = {0x4d420000, 0, 0, 94, 40, ABG_XRes, ABG_YRes, 0x00040001, 2, 0, 0, 0, 10, 10 };
    uint8_t bmp_emptyline[8] = {0xfe, 0, 0xfe, 0, ABG_XRes-(2*0xfe), 0, 0, 0};
    uint8_t bmp_ende[2] = {0, 1};

	// schwarz finden, und als Leerzeilen-Farb-Index setzen
	uint32_t m = bmp_palette[0];
	uint8_t f = 0;
	for (uint8_t i=1; i<4; i++)
	{
		if (bmp_palette[i]<m)
		{
			m = bmp_palette[i];
			f = i;
		}
	}
	f = f | f<<4;
	bmp_emptyline[1] = f;
	bmp_emptyline[3] = f;
	bmp_emptyline[5] = f;

    uint8_t framecount = 10;

    while (1)
    {
		// alle 10 Frames die Stepliste aktualisieren
        if (framecount >= 10)
        {
            update_pixel_steplist();
            framecount = 0;
        }

		// BMP capture
        web_capture_bmp_image();

		// BMP länge zusammenrechnen
        uint32_t len = 2;
        for (uint32_t i=0;i<ABG_YRes;i++)
        {
            if (bmp_line_length[i] == 0)
            {
                len += 8;
            }
            else
            {
                len += bmp_line_length[i];
            }
        }

		// Header ergänzen
        bmp_header[9] = len;
        len += 94;
        bmp_header[1] = len;

        gpio_set_level(PIN_NUM_LED_WIFI,0);

		// BMP senden
        char size_buf[12];
        if (httpd_send(req, "\r\n--" STREAM_CONTENT_BOUNDARY "\r\nContent-Type: image/bmp\r\nContent-Length: ",77) < 0) break;
        uint16_t l = sprintf(size_buf, "%ld\r\n\r\n", len);
        if (httpd_send(req, size_buf, l) < 0) break;
        if (httpd_send(req, ((char*)bmp_header)+2, 54) < 0) break;
        if (httpd_send(req, (char*)bmp_palette, 40) < 0) break;
        for (int32_t i=ABG_YRes-1;i>=0;i--)
        {
            if (bmp_line_length[i] == 0)
            {
                if (httpd_send(req, (char*)bmp_emptyline, 8) < 0) break;
            }
            else
            {
                if (httpd_send(req, (char*)&bmp_img[1024*i], bmp_line_length[i]) < 0) break;
            }
        }
        if (httpd_send(req, (char*)bmp_ende, 2) < 0) break;
        gpio_set_level(PIN_NUM_LED_WIFI,1);
        framecount++;
    }
    gpio_set_level(PIN_NUM_LED_WIFI,1);
	gpio_set_level(PIN_NUM_LED_SYNC,0);
    return ESP_OK;
}

// Webseiten-Handler BMP-Screenshot
static esp_err_t bmp_get_handler(httpd_req_t *req)
{
    uint32_t bmp_header[14] = {0x4d420000, 0, 0, 94, 40, ABG_XRes, ABG_YRes, 0x00040001, 2, 0, 0, 0, 10, 10 };
    uint8_t bmp_emptyline[8] = {0xfe, 0, 0xfe, 0, ABG_XRes-(2*0xfe), 0, 0, 0};
    uint8_t bmp_ende[2] = {0, 1};
	update_pixel_steplist();
	web_capture_bmp_image();
    httpd_resp_set_type(req, "image/bmp");
	httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=Screenshot.bmp");
	httpd_resp_send_chunk(req, ((char*)bmp_header)+2, 54);
	httpd_resp_send_chunk(req, (char*)bmp_palette, 40);
	for (int32_t i=ABG_YRes-1;i>=0;i--)
	{
		if (bmp_line_length[i] == 0)
		{
			httpd_resp_send_chunk(req, (char*)bmp_emptyline, 8);
		}
		else
		{
			httpd_resp_send_chunk(req, (char*)&bmp_img[1024*i], bmp_line_length[i]);
		}
	}
	httpd_resp_send_chunk(req, (char*)bmp_ende, 2);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// Webseiten-Handler Hauptseite
static esp_err_t mainpage_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><head></head><body><img src=\"stream.mbmp\" height=\"800\" /></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static const httpd_uri_t bmpstream = {
    .uri       = "/stream.mbmp",
    .method    = HTTP_GET,
    .handler   = mbmp_get_handler,
};

static const httpd_uri_t mainpage = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = mainpage_get_handler,
};

static const httpd_uri_t bmpshot = {
    .uri       = "/screenshot.bmp",
    .method    = HTTP_GET,
    .handler   = bmp_get_handler,
};

// Webserver starten
void start_webserver()
{
	if (web_server != NULL)
	{
		httpd_stop(web_server);
		web_server = NULL;
	}
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.core_id = 0;

    if (httpd_start(&web_server, &config) != ESP_OK) 
    {
		web_server = NULL;
        return;
    }
    httpd_register_uri_handler(web_server, &bmpstream);
    httpd_register_uri_handler(web_server, &mainpage);
    httpd_register_uri_handler(web_server, &bmpshot);
}


void stop_webserver()
{
	if (web_server != NULL)
	{
		httpd_stop(web_server);
		web_server = NULL;
	}
}