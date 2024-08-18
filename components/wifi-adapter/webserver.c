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
#include "wlanpasswd.h"

static int s_retry_num = 0;
static httpd_handle_t web_server = NULL;
static wifi_config_t wifi_config =
{
	.sta = {
		.ssid = WLAN_SSID,
		.password = WLAN_PASSWORD,
		.threshold.authmode = WIFI_AUTH_WPA2_PSK,
		.sae_pwe_h2e = WPA3_SAE_PWE_HUNT_AND_PECK,
		.sae_h2e_identifier = "",
	},
};
static esp_wps_config_t wps_config = WPS_CONFIG_INIT_DEFAULT(WPS_TYPE_PBC);

#define STREAM_CONTENT_BOUNDARY "638974789000000000000987654321"
void start_webserver();

// Status in die OSD Statuszeile drucken
static void printstatus(char* txt)
{
	char tb[10];
	int l = snprintf(tb,10,txt);
	drawstate(tb, l, 44, 10);
}

// IP in die OSD Statuszeile drucken
static void printip(char* txt)
{
	char tb[16];
	int l = snprintf(tb,16,txt);
	drawstate(tb, l, 59, 16);
}

// wlan Handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
	switch (event_id)
	{
		case WIFI_EVENT_STA_START:
	        printf("WIFI_EVENT_STA_START\n");
			esp_wifi_connect();
			printip("keine");
			printstatus("verbinde");
			break;
		case WIFI_EVENT_STA_DISCONNECTED:
	        printf("WIFI_EVENT_STA_DISCONNECTED\n");
			gpio_set_level(PIN_NUM_LED_WIFI,0);
			if (web_server)
			{
				httpd_stop(web_server);
				web_server = NULL;
			}
			if (s_retry_num < 10) 
			{
				esp_wifi_connect();
				s_retry_num++;
				printstatus("verbinde");
			} 
			else 
			{
				printstatus("getrennt");
			}
			printip("keine");
			break;
        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
	        printf("WIFI_EVENT_STA_WPS_ER_SUCCESS\n");
			wifi_event_sta_wps_er_success_t *evt = (wifi_event_sta_wps_er_success_t *)event_data;
			if (evt) 
			{
				memcpy(wlan_ssid, evt->ap_cred[0].ssid, 32);
				memcpy(wlan_passwd, evt->ap_cred[0].passphrase, 64);
				memcpy(wifi_config.sta.ssid,wlan_ssid,32);
				memcpy(wifi_config.sta.password,wlan_passwd,64);
				ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
			}
			else
			{
				ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &wifi_config));
				memcpy(wlan_ssid, wifi_config.sta.ssid, 32);
				memcpy(wlan_passwd, wifi_config.sta.password, 64);
			}
			wps_mode = 0;
			ESP_ERROR_CHECK(esp_wifi_wps_disable());
			drawstate(wlan_ssid, strlen(wlan_ssid), 5, 32);
			nvs_set_u8(sys_nvs_handle, _NVS_SETTING_WPS_MODE, 0);
			nvs_set_str(sys_nvs_handle, _NVS_SETTING_SSID, wlan_ssid);
			nvs_set_str(sys_nvs_handle, _NVS_SETTING_PASSWD, wlan_passwd);
			printstatus("verbinde");
			esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_WPS_ER_FAILED:
	        printf("WIFI_EVENT_STA_WPS_ER_FAILED\n");
			s_retry_num++;
            ESP_ERROR_CHECK(esp_wifi_wps_disable());
			if (s_retry_num <=5)
			{
				printstatus("WPS retry");
				ESP_ERROR_CHECK(esp_wifi_wps_enable(&wps_config));
				ESP_ERROR_CHECK(esp_wifi_wps_start(0));
			}
			else
			{
				wps_mode = 0;
				nvs_set_u8(sys_nvs_handle, _NVS_SETTING_WPS_MODE, 0);
				printstatus("WPS Error");
			}
            break;
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
	        printf("WIFI_EVENT_STA_WPS_ER_TIMEOUT\n");
			s_retry_num++;
            ESP_ERROR_CHECK(esp_wifi_wps_disable());
			if (s_retry_num <=5)
			{
				printstatus("WPS retry");
				ESP_ERROR_CHECK(esp_wifi_wps_enable(&wps_config));
				ESP_ERROR_CHECK(esp_wifi_wps_start(0));
			}
			else
			{
				wps_mode = 0;
				nvs_set_u8(sys_nvs_handle, _NVS_SETTING_WPS_MODE, 0);
				printstatus("WPS Zeit");
			}
            break;
        default:
            break;			
	}
}

// Handler connect
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) 
    {
		gpio_set_level(PIN_NUM_LED_WIFI,1);
		if (web_server == NULL) 
		{
			start_webserver();
		}
		printstatus("verbunden");
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
		char tb[16];
		int l = snprintf(tb,16,"%d.%d.%d.%d", IP2STR(&event->ip_info.ip));
		drawstate(tb, l, 59, 16);
        s_retry_num = 0;
        printf("IP_EVENT_STA_GOT_IP (%d.%d.%d.%d)\n", IP2STR(&event->ip_info.ip));
    }
}

// Webseiten-Handler BMP-Stream
static esp_err_t IRAM_ATTR mbmp_get_handler(httpd_req_t *req)
{
    httpd_send(req, "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: multipart/x-mixed-replace; boundary=" STREAM_CONTENT_BOUNDARY "\r\n",131);
    uint32_t bmp_header[14] = {0x4d420000, 0, 0, 94, 40, ABG_XRes, ABG_YRes+20, 0x00040001, 2, 0, 0, 0, 10, 10 };
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
		bool old_osd_bmp_index = osd_bmp_index;
		if (osd_aktiv)
		{
			if (old_osd_bmp_index)
			{
				for (int32_t i=39;i>=20;i--)
				{
					len += osd_bmp_length[i];
				}
			}
			else
			{
				for (int32_t i=19;i>=0;i--)
				{
					len += osd_bmp_length[i];
				}
			}
		}
		else
		{
			len += 8*20;
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
		if (osd_aktiv)
		{
			if (old_osd_bmp_index)
			{
				for (int32_t i=39;i>=20;i--)
				{
					if (httpd_send(req, (char*)&osd_bmp_img[1024*i], osd_bmp_length[i]) < 0) break;
				}
			}
			else
			{
				for (int32_t i=19;i>=0;i--)
				{
					if (httpd_send(req, (char*)&osd_bmp_img[1024*i], osd_bmp_length[i]) < 0) break;
				}
			}
		}
		else
		{
			for (int32_t i=0;i<20;i++)
			{
                if (httpd_send(req, (char*)bmp_emptyline, 8) < 0) break;
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

// Webserver starten
void start_webserver()
{
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
}

// wlan starten, entweder WPS mode, oder Anmeldung am Router
void setup_wlan()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL));

	s_retry_num = 0;
	if (wps_mode == 1)
	{
		ESP_ERROR_CHECK(esp_wifi_start());
		ESP_ERROR_CHECK(esp_wifi_wps_enable(&wps_config));
		ESP_ERROR_CHECK(esp_wifi_wps_start(0));
		printstatus("WPS start");
		while (wps_mode == 1)
		{
			usleep(50000);
			gpio_set_level(PIN_NUM_LED_WIFI, 1);
			usleep(50000);
			gpio_set_level(PIN_NUM_LED_WIFI, 0);
		}
	}
	else
	{
		memcpy(wifi_config.sta.ssid,wlan_ssid,32);
		memcpy(wifi_config.sta.password,wlan_passwd,64);
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
		ESP_ERROR_CHECK(esp_wifi_start());
	}
}
