#pragma once
#include <esp_heap_caps.h>

void setup_flash();
bool restore_settings();
bool write_settings(bool full);


extern void osd_task(void*);
extern void drawstate(char* txt, int count, int pos, int fill);
