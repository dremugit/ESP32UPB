#define ESP32UPBerrorsversion 11

uint16_t error=0;
uint32_t lasterrorblink=0;

// prototypes

void setLEDs(); // in ESP32UPBcore.h

/*
EEPROM locations:

1CC-1CD		error blink length
1CE				error number
1D0-1D1		parameter 1
1D2-1D3		parameter 2

1CF				reboot flag

general errors:

number	cycle		meaning
100			100			WiFi init failure
101			150			WiFi disconnect
102			250			invalid command
103			150			MQTT disconnect
104			150			MQTT publish failure
105			50			EEPROM checksum failure
 
error codes 1-99 are per-device

shades

2				pos		target	div by zero in dimlevel
4				pos		0				init up stop
5				pos		0				init down stop
7				0			0 			init zero stop
8				run		0 			motor timeout
9				pos		0 			over position
10			draw	pos			over current
11			speed	0 			under speed
12			mS		pos 		pulse timeout
14			pos		0				loop up stop
15			pos		0				loop down stop
16			pos		0				over speed



if error code has high bit set, one of the parameters was more than 0xFFFF and so didn't fit into 1D0-1D1 or 1D2-1D3
in theory, such cases should be handled in the device code, making this a "This Never Happens", but...

*/

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void seterror(uint16_t errorlength, uint8_t errornumber)
{
	error=errorlength;
	EEPROM.write(0x1CC,errorlength >> 8);
	EEPROM.write(0x1CD,errorlength & 0xFF);
	EEPROM.write(0x1CE,errornumber);

	EEPROM.commit();
	#if (DEBUG_LEVEL > 0)
		Serial.printf("Set error number %d\n",errornumber);
		Serial.printf("Set error length %d\n",errorlength);
	#endif
	
	if (errornumber==0)
	{
		redLEDval=0;
		setLEDs();
	}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void clearstorederror(void)
{
	// write to EEPROM only if previous error
	error=0;
	
	if (EEPROM.read(0x1CE))
	{
		EEPROM.write(0x1CE,0x00);
		// clear errors from library, WiFi, etc
		// but keep device-specific numbers
	//	if (EEPROM.read(0x1CE) >= 100) {seterror(0,0);}
		seterror(0,0);
		commitflag=true;
	}
	redLEDval=0;
	setLEDs();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
