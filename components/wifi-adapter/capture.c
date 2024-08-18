#include <math.h>
#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"
#include <driver/spi_master.h>
#include <xtensa_context.h>
#include <soc/gpio_reg.h>
#include <soc/gdma_reg.h>
#include <soc/spi_reg.h>
#include "esp_intr_alloc.h"
#include <rom/ets_sys.h>
#include <string.h>
#include "esp_private/gdma.h"
#include "driver/gpio_filter.h"
#include "esp_ota_ops.h"

#include "capture.h"
#include "globalvars.h"
#include "main.h"
#include "pins.h"
#include "osd.h"

// einen bestimmten DMA-Kanal reservieren
void alloc_dma(uint8_t channel)
{
	gdma_channel_handle_t s_rx_channel;
	gdma_channel_alloc_config_t rx_alloc_config = 
	{
		.direction = GDMA_CHANNEL_DIRECTION_RX,
		.sibling_chan = NULL,
	};
	if (gdma_new_channel(&rx_alloc_config, &s_rx_channel) != ESP_OK) return;
	int i=10;
	gdma_get_channel_id(s_rx_channel, &i);
	if (i != channel)
	{
		alloc_dma(channel);
		gdma_del_channel(s_rx_channel);
	}
}

// alles für die Abtastung der Video-Signale vom ABG mit SPI vorbereiten
void setup_abg()
{
	// DMA-Kanal 1 reservieren
	alloc_dma(1);

	// Buffer holen
	ABG_DMALIST = heap_caps_malloc(16 * _ABG_SAMPLE_BUFFER_COUNT, MALLOC_CAP_DMA | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
	PIXEL_STEP_LIST = heap_caps_malloc(ABG_XRes, MALLOC_CAP_DMA | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);

	// Aus dieser Tabelle nimmt der DMA-Kontroller die Speicheraddressen
	for (int i=0;i<_ABG_SAMPLE_BUFFER_COUNT;i++)
	{
		ABG_DMALIST[i*4] = 0xc0000000;
		ABG_DMALIST[i*4+1] = (uint32_t)heap_caps_malloc(4096, MALLOC_CAP_DMA | MALLOC_CAP_32BIT | MALLOC_CAP_INTERNAL);
		ABG_DMALIST[i*4+2] = 0;
		ABG_DMALIST[i*4+3] = 0;
	}

	// Pin-Konfiguration
	spi_bus_config_t buscfg=
	{
		.data0_io_num = 0,
		.data1_io_num = PIN_NUM_ABG_VIDEO1,  // Farb-Signal
    	.data2_io_num = PIN_NUM_ABG_VIDEO2,  // Farb-Signal
    	.data3_io_num = PIN_NUM_ABG_BSYNC1,  // wir zeichnen auch das BSYN auf, daran können wir später die Spalten ausrichten
    	.max_transfer_sz = 4096,
    	.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };
	ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

	// SPI-Konfiguration
	spi_device_interface_config_t devcfg=
    {
    	.cs_ena_pretrans = 0,
		.cs_ena_posttrans = 0,
        .clock_speed_hz = SPI_MASTER_FREQ_80M, // Abtast-Frequenz 80MHz - das ist mehr als wir eigendlich brauchen,
        .flags = SPI_DEVICE_HALFDUPLEX,        // aber mit weniger gäbe es Probleme mit der Pixelsynchronisation
	    .queue_size=1,
#ifdef DEBUG
		.spics_io_num = PIN_NUM_DEBUG_SPI,     // zur Fehlersuche: an diesem Pin kann man sehen, wann das SPI aktiv ist
#endif
    };
	spi_device_handle_t spi;
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &spi));

    // Transfer-Konfiguration
	spi_transaction_t transfer =
	{
	    .flags = (ABG_Bits_per_sample == 4) ? SPI_TRANS_MODE_QIO : SPI_TRANS_MODE_OCT,           // 4 oder 8 Bit gleichzeitig einlesen
	    .length = 0,                           // nix ausgeben...
	    .rxlength = 4096 * 8,
	    .rx_buffer = (uint32_t*)ABG_DMALIST[5],
	};

	// Transfer starten. Wir brauchen die Daten hier noch nicht, aber der Transfer setzt alle Parameter in dem SPI-Kontroller
	ESP_ERROR_CHECK(spi_device_queue_trans(spi, &transfer, 0));

	// wir haben uns den DMA-Kanal 1 reserviert, nun weisen wir den Kanal wieder dem SPI-Kontroller zu
	if (REG_READ(GDMA_IN_PERI_SEL_CH0_REG) == 0) REG_WRITE(GDMA_IN_PERI_SEL_CH0_REG, 63);
	REG_WRITE(GDMA_IN_PERI_SEL_CH1_REG, 0);
	if (REG_READ(GDMA_IN_PERI_SEL_CH2_REG) == 0) REG_WRITE(GDMA_IN_PERI_SEL_CH2_REG, 63);
	if (REG_READ(GDMA_IN_PERI_SEL_CH3_REG) == 0) REG_WRITE(GDMA_IN_PERI_SEL_CH3_REG, 63);

	// DMA-Kontroller 1 auf unseren Puffer umstellen und starten
	REG_SET_BIT(GDMA_IN_LINK_CH1_REG, GDMA_INLINK_STOP_CH1);
	REG_SET_BIT(GDMA_IN_CONF1_CH1_REG, GDMA_IN_CHECK_OWNER_CH1);
	REG_SET_FIELD(GDMA_IN_LINK_CH1_REG, GDMA_INLINK_ADDR_CH1, (int)ABG_DMALIST);
	REG_SET_BIT(GDMA_IN_LINK_CH1_REG, GDMA_INLINK_START_CH1);

	// jetzt noch die restlichen GPIO's konfigurieren
	// SPI und Interrupt gleichzeitig auf dem selben PIN klappt nicht, wir nehmen deshalb 2 getrennte Pins außen verbunden.
	gpio_config_t pincfg =
	{
	    .pin_bit_mask = 1ULL << PIN_NUM_ABG_BSYNC2,
	    .mode = GPIO_MODE_INPUT,
	    .pull_up_en = false,
	    .pull_down_en = false,
	    .intr_type = GPIO_INTR_NEGEDGE,
	};
	ESP_ERROR_CHECK(gpio_config(&pincfg));

	// Glitch-Filter für das BSYNC-Signal aktivieren, das reduziert Fehl-Auslösungen des IRQ
    gpio_glitch_filter_handle_t filter = NULL;
    gpio_pin_glitch_filter_config_t config = 
	{
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
		.gpio_num = PIN_NUM_ABG_BSYNC1,
    };
    ESP_ERROR_CHECK(gpio_new_pin_glitch_filter(&config, &filter));
    ESP_ERROR_CHECK(gpio_glitch_filter_enable(filter));
	config.gpio_num = PIN_NUM_ABG_BSYNC2;
    ESP_ERROR_CHECK(gpio_new_pin_glitch_filter(&config, &filter));
    ESP_ERROR_CHECK(gpio_glitch_filter_enable(filter));


	ESP_INTR_DISABLE(31);
	intr_matrix_set(xPortGetCoreID(), ETS_GPIO_INTR_SOURCE, 31);  // Assembler ISR aktivieren
	ESP_INTR_ENABLE(31);

#ifdef PIN_NUM_DEBUG_COPY
	// noch ein Pin zur Fehlersuche: das zeigt an, wenn das Programm Daten von dem SPI Puffer in den VGA Puffer kopiert.
	gpio_config_t pincfg2 =
	{
	    .pin_bit_mask = 1ULL << PIN_NUM_DEBUG_COPY,
	    .mode = GPIO_MODE_OUTPUT,
	    .pull_up_en = false,
	    .pull_down_en = false,
	};
	ESP_ERROR_CHECK(gpio_config(&pincfg2));
#endif

	// Pin für die Sync-Status-LED aktivieren
	gpio_config_t pincfg3 =
	{
	    .pin_bit_mask = 1ULL << PIN_NUM_LED_WIFI | 1ULL << PIN_NUM_LED_SYNC,
	    .mode = GPIO_MODE_OUTPUT,
	    .pull_up_en = false,
	    .pull_down_en = false,
	};
	ESP_ERROR_CHECK(gpio_config(&pincfg3));
	gpio_set_level(PIN_NUM_LED_WIFI,0);
	gpio_set_level(PIN_NUM_LED_SYNC,0);
}

// Die Pixel-Positionen innerhalb der Samples neu berechnen
void IRAM_ATTR update_pixel_steplist()
{
	// :200 Zeilen
	// :240 MHz CPU-Takt
	// *80 MHz Sample Rate
	// = 600
	// :ABG_PIXEL_PER_LINE
	bsyn_clock_last = bsyn_clock_diff;
	double weite = bsyn_clock_diff / (600.0f * ABG_PIXEL_PER_LINE);
	double abst = 0.0f;
	double step = modf((weite * ((double)(ABG_PIXEL_PER_LINE - BSYNC_PIXEL_ABSTAND))), &abst);
	BSYNC_SAMPLE_ABSTAND = (int)abst;
	int istep = round(step); 
	int last = istep;
	for (int i=0;i<ABG_XRes;i++)
	{
		step += weite;
		istep = round(step);
		PIXEL_STEP_LIST[i] = istep - last;
		last = istep;
	}
}

// Alle Zeilenlängen löschen - das leert den BMP-Zeilenpuffer
void IRAM_ATTR clear_bmp_lines()
{
	for (uint32_t i=0;i<ABG_YRes;i++)
	{
		bmp_line_length[i] = 0;
	}
}

// Pixeldaten aus dem Sample-Buffer kopieren und BMP-Lauflängenkodieren diese Funktion wird von beiden Cores gleichzeitig ausgeführt
void IRAM_ATTR web_capture_line()
{
	// einen vollen Sample-Buffer suchen
    volatile uint32_t* next = NULL;
    for (uint8_t z=0;z<_ABG_SAMPLE_BUFFER_COUNT;z++)
    {
        if ((ABG_DMALIST[z*4] & 0x80000000) == 0)
        {
            next = &ABG_DMALIST[z*4];
            ABG_DMALIST[z*4] = ABG_DMALIST[z*4] | 0x80000000;
            break;
        }
    }

    if (next == NULL) return; // keinen vollen Sample-Buffer gefunden?
    
	uint32_t line = next[3];

	// Scanline außerhalb des Bildbereichs?
	if (line>ABG_START_LINE+(ABG_YRes-1)) return;
	if (line<ABG_START_LINE) return;
	
	// Anzahl der empfangenen Bytes, wird vom DMA-Kontroller gesetzt
	uint32_t b=(*next & 0xfff000) >> 12;

	uint32_t sync = 0;
	uint8_t* buf = (uint8_t*)next[1];

	// Wir suchen den nächsten BSYNC-Impuls
	if (ABG_Bits_per_sample == 4)
	{
		int a=b-25;
		while ((buf[a] & 0x88)!=0x88 && a>0)
		{
			a--;
		}
		for (; a<b; a++)
		{
			if ((buf[a] & 0x88)!=0x88)
			{
				sync = a * 2;  // gefunden!
				if ((buf[a] & 0x88) == 0x80)
				{
					sync++;
				}
				break;
			}
		}
	}
	else
	{
		int a=b-50;
		while ((buf[a] & 0x08)!=0x08 && a>0)
		{
			a--;
		}
		for (; a<b; a++)
		{
			if ((buf[a] & 0x08)!=0x08)
			{
				sync = a;  // gefunden!
				break;
			}
		}
	}

	// und nun die Pixel in den VGA-Puffer kopieren
	if (sync>0)
	{
		if (ABG_Bits_per_sample == 4)
		{
			uint32_t bufpos = sync - BSYNC_SAMPLE_ABSTAND;
			uint8_t* bmpst = (uint8_t*)(((line - ABG_START_LINE)*1024) + (int)bmp_img);
			uint8_t* bmppos = bmpst;
			uint8_t* stepende = (uint8_t*)((int)PIXEL_STEP_LIST + ABG_XRes);
			uint8_t val1 = 0;
			uint8_t val2 = 0;
			uint8_t rep = 0;
			uint8_t dat = (bufpos & 1) ? (((*((bufpos>>1)+buf))>>1) & 3) : (((*((bufpos>>1)+buf))>>5) & 3);

			for (uint8_t* steplist=PIXEL_STEP_LIST;steplist<stepende;)
			{
				if (rep == 0)
				{
					val1 = dat;
					bufpos+=*steplist;
					steplist++;
					val2 = (bufpos & 1) ? (((*((bufpos>>1)+buf))>>1) & 3) : (((*((bufpos>>1)+buf))>>5) & 3);
					bufpos+=*steplist;
					steplist++;
					rep = 2;
					dat = (bufpos & 1) ? (((*((bufpos>>1)+buf))>>1) & 3) : (((*((bufpos>>1)+buf))>>5) & 3);
				}
				if (dat!=val1) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = val2 | (val1<<4);
					bmppos++;
					rep = 0;
					continue;
				}
				else
				{
					bufpos+=*steplist;
					dat = (bufpos & 1) ? (((*((bufpos>>1)+buf))>>1) & 3) : (((*((bufpos>>1)+buf))>>5) & 3);
					steplist++;
					rep++;
				}
				if (dat!=val2) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = val2 | (val1<<4);
					bmppos++;
					rep = 0;
					continue;
				}
				else
				{
					bufpos+=*steplist;
					dat = (bufpos & 1) ? (((*((bufpos>>1)+buf))>>1) & 3) : (((*((bufpos>>1)+buf))>>5) & 3);
					steplist++;
					rep++;
				}
				if (rep>253) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = val2 | (val1<<4);
					bmppos++;
					rep = 0;
				}
			}
			if (rep>0)
			{
				*bmppos = rep;
				bmppos++;
				*bmppos = val2 | (val1<<4);
				bmppos++;
			}
			*bmppos = 0;
			bmppos++;
			*bmppos = 0;
			bmppos++;
			bmp_line_length[line-ABG_START_LINE] = bmppos - bmpst;
		}
		else // 8 bit samples
		{
			uint8_t* bufpos = (uint8_t*)((sync - BSYNC_SAMPLE_ABSTAND) + (int)buf);
			uint8_t* bmpst = (uint8_t*)(((line - ABG_START_LINE)*1024) + (int)bmp_img);
			uint8_t* bmppos = bmpst;
			uint8_t* stepende = (uint8_t*)((int)PIXEL_STEP_LIST + ABG_XRes);
			uint8_t val1 = 0;
			uint8_t val2 = 0;
			uint8_t rep = 0;

			for (uint8_t* steplist=PIXEL_STEP_LIST;steplist<stepende;)
			{
				if (rep == 0)
				{
					val1 = *bufpos;
					bufpos+=*steplist;
					steplist++;
					val2 = *bufpos;
					bufpos+=*steplist;
					steplist++;
					rep = 2;
				}
				if (*bufpos!=val1) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = ((val2>>1)&3) | ((val1<<3)&0x30);
					bmppos++;
					rep = 0;
					continue;
				}
				else
				{
					bufpos+=*steplist;
					steplist++;
					rep++;
				}
				if (*bufpos!=val2) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = ((val2>>1)&3) | ((val1<<3)&0x30);
					bmppos++;
					rep = 0;
					continue;
				}
				else
				{
					bufpos+=*steplist;
					steplist++;
					rep++;
				}
				if (rep>253) 
				{
					*bmppos = rep;
					bmppos++;
					*bmppos = ((val2>>1)&3) | ((val1<<3)&0x30);
					bmppos++;
					rep = 0;
				}
			}
			if (rep>0)
			{
				*bmppos = rep;
				bmppos++;
				*bmppos = ((val2>>1)&3) | ((val1<<3)&0x30);
				bmppos++;
			}
			*bmppos = 0;
			bmppos++;
			*bmppos = 0;
			bmppos++;
			bmp_line_length[line-ABG_START_LINE] = bmppos - bmpst;
		}
	}
	*next=0xc0000000;
}

// Pixelkopier und Komprimier-Task, läuft auf core 1
bool volatile Task2Running = false;
void IRAM_ATTR web_capture_task(void*)
{
	Task2Running = true;
	while (ABG_RUN)
	{
        web_capture_line();
    }
	Task2Running = false;
    vTaskDelete(NULL);
}

// Pixelkopier und Komprimier-Task, läuft auf core 0
void IRAM_ATTR web_capture_bmp_image()
{
	// bmp clear
	clear_bmp_lines();

	// sample buffer clear
    for (uint8_t z=0;z<_ABG_SAMPLE_BUFFER_COUNT;z++)
    {
        ABG_DMALIST[z*4] = 0xc0000000;
    }

	// warten auf Scanline 1
    uint32_t a = 1000000;
	while (ABG_Scan_Line != 1)
    {
        a--;
        if (a==0)
        {
			// timeout!
            return;
        }
    }

	// Sampling-Prozess im ISR aktivieren
    ABG_RUN = true;
	gpio_set_level(PIN_NUM_LED_SYNC,1);
	
	// wir lassen uns vom core 1 helfen...
    xTaskCreatePinnedToCore(web_capture_task,"web_capture_task",6000,NULL,10,NULL,1);

    uint16_t z = 0;
    uint16_t f = 0;
    a = 100000;
    bool t = false;
    while (z<ABG_YRes)
    {
        if (ABG_Scan_Line < 5)
        {
            if (t && ABG_Scan_Line>3)
            {
                z = 0;
                if (ABG_Last_Scan_Line == ABG_Total_Scanlines)
                {
					ABG_Last_Scanl_Repeat = ABG_Last_Scan_Line;
                    for (uint32_t i=0;i<ABG_YRes;i++)
                    {
                        if (bmp_line_length[i] != 0) z++;
                    }
                }
                else
                {
					if (ABG_Last_Scanl_Repeat == ABG_Last_Scan_Line)
					{
						if (ABG_Total_Scanl_Repeat > 5)
						{
							ABG_Total_Scanlines = ABG_Last_Scanl_Repeat;
						}
						else
						{
							ABG_Total_Scanl_Repeat++;
						}
					}
					else
					{
						ABG_Last_Scanl_Repeat = ABG_Last_Scan_Line;
						clear_bmp_lines();
					}
                }
                t = false;
                f++;
            }
        }
        else
        {
            t = true;
        }

        web_capture_line();

        a--;
        if (a==0)
        {
			break;
        }
    }
    ABG_RUN = false;
	while (Task2Running);
}
