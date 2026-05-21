# ESP32UPB
Wireless extension on Arduino ESP32 to the UPB home automation platform

For all, edit ESP32UPBsecrets.h as to your specific network details (IP addresses, WiFi connection)

First, build bridge.
	parts
		RS232 PIM with a DB9 (either SAI or PCS. HAI would require different cable for its RJ14)
		ESP32S, specifically, aka "ESP32-WROOM", *not* ESP32-S3 or any other ESP32-letter. the 38-pin are common and cheap, but the 30-pin on a breakout board can be handy too.
			Watch the breakout boards as some LOOK like they're Arduino form factor but aren't quite, so they don't fit neatly into Arduino cases
		ESP32S aka "ESP32WROOM", *not* an S3 or any other type. the 38-pin are cheap as dirt, but the 30-pin on a breakout board can be handy too.
		TTL-to-RS232 converter like https://www.amazon.com/Anmbest-Converter-Connector-Raspberry-Microcontrollers/dp/B07LBDZ9WG
			as example. never actually bought that particular one.
		Recommend DB9M, either to attach directly to the PIM or use a short DB9 M-F cable. 
		Mixing and matching types on RS232 can lead to confusion as to Tx and Rx pins.
		handful of Dupont jumper cables. Any real Arduino nerd will have a pile.
		USB power supply, as used for phone charger
		short USB cable from the charger to whatever USB plug the ESP32 has (micro USB, USB-C)

hardware
	Wire DB9 converter to 3v3 and ground on ESP32, Rx to 19, Tx to 18

software
	load and upload wupb_bridge_13 sketch
	watch serial monitor for complaints. at default DEBUG_LEVEL 1 should see UPB line traffic as Line -> MQTT 12345678...
	if no traffic, swap 18 and 19 in sketch, re-upload

	recommend once it shows data, to disconnect it from the computer. with many ESP32's on the desk it's easy to get them confused and upload a sketch to the wrong unit.

Second, build device
	parts
		second ESP32 (same as above)
		momentary pushbotton switch on pin 18 and ground
	software
		upload set_eeprom_140t_new_device. this sets the EEPROM as if it were an SAI US140-T, as device 254 (unprogrammed)
		upload demo_device. this is the actual code for the US140, and the demo is the bare minimum code required to work. It will turn the onboard LED on and off as the load.
			verify it has loaded by going to my.sub.net.254 in your web browser. It should have a web page, albeit with a message at the bottom about "error 255". Clicking Clear error will remove that.
			clicking "On" and "Off" under "Currently (whichever)" should turn the LED on and off.
			clicking "Enter setup mode" should blink the LED, as should pressing the pushbutton for about five seconds.

Third, build WUPB programmer
	parts
		third ESP32 (as above)
		upload PIM_emulator_for_WUPB_programming_1. do not open the serial monitor as UPStart will need to use that port.
		Launch UPStart. Disconnect it from existing PIM, and configure UPStart to use whatever port (e.g. COM4, /dev/ttyS0, whatever) to which the PIM emulator was just uploaded


To add device to network, put device into setup mode. either launch its web interface and choose "enter setup mode", or press and hold the momentary switch on pin 18 for about five seconds. the onboard LED should start blinking.

Add the device in UPStart. It may fail to find the module in setup the first time; Retry should resolve this. UPStart's serial interface can be persnickety (but the PIM-IP is even more so.)

Assuming it added successfully, the device can be turned on and off from UPStart, programmed to respond to links, whatever.

