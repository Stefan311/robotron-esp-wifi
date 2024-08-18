# Robotron WLAN Capture Adapter

This project is based on the work of Stefan "Ordoban" Berndt.

ESP32S3 based MDA signal to Webseite adapter

Supported systems:
A7100
PC1715
EC1834 MON Adapter

Published at:
https://www.robotrontechnik.de/html/forum/thwb/showtopic.php?threadid=21380

PCB is here:
https://github.com/mgoegel/robotron-esp-vga.pcb

This is an alternative firmware for the robotron-esp-vga adapter, enable the adapter to stream the captured image as a webpage over wifi.
The device does not support VGA and WIFI at the same time, it does not even allow both features combined in one firmware.

So this firmware does not output the screen capture as VGA signal. Only the OSD menu is displayed.
On the device can the VGA or WIFI firmware be flashed - or even both. If more than one firmware is detected, there is a OSD menu option to chose the active one-.

To flash only one, just use the regular ESP32 project flash procedure.

To flash both, (or even more!) use this method:
1. Compile the first firmware. Examine the compiler output, there should be something like:
```
Project build complete. To flash, run:
ESP-IDF: Flash your project in the ESP-IDF Visual Studio Code Extension
or in a ESP-IDF Terminal:
idf.py flash
or
idf.py -p PORT flash
or
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset  --port /dev/ttyACM0 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x0 bootloader/bootloader.bin 0x10000 vga-adapter.bin 0x8000 partition_table/partition-table.bin 0xd000 ota_data_initial.bin 
or from the "/home/stefan/A7100/vga-git/robotron-esp-wifi/build" directory
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"
```
2. Flash the first firmware on the regular way
3. Compile the second firmware
4. modify and run one of the above suggested flash commands (this may differ on your system!)
```
python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset  --port /dev/ttyACM0 write_flash --flash_mode dio --flash_size 4MB --flash_freq 80m 0x110000 vga-adapter.bin
```
This flashes only the main application file to a free memory offset, leaves the bootloader, partition table and main app of the first firmware version intact.

There are two ways of enter the WIFI credentials:
- Enter them in the wlanpasswd.h file before compile and flash
- Use the WPS feature:
1. activate the WPS mode on your wifi router
2. push the left-button twice, then hold the left-button 5 sekunds. The wifi-led should flash 3s slow, then 2s faster, then turn off, then flash very fast. If the wifi-led stays on, the wifi is connected. If the led turns off, the WPS connection failed.

To view the captured stream, just enter the IP of the device in your browser. The IP can be seen in the OSD menu, in the debug output, or in the device table of your router.
