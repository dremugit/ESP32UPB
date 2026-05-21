#define ESP32US140Tversion 300

/*
Universal Powerline Bus emulation of Simply Automated US1-40T dimmer switch (firmware 02.22)
7 july 2023

This allows ESP32 control of non-UPB devices: blinds, occupancy sensors, really anything that requires more intelligence than the regular UPB I/O devices can provide.

The US1-40T was chosen to emulate as it's fairly flexible in terms of receive and transmit choices versus an input- or output-only device. Examples:

o  special lighting or motorized blinds would use the output dim level, ignoring transmit components
o  a temperature sensor would transmit at specified levels, particular intervalk, or only when sent a Get Status Report
o  an occupancy sensor could use the receive components to enable and disable or set timeouts
     and the transmit components to indicate what link(s) to send when

The EEPROM mirrors the first 255 registers of the SA US140T in 00-FE
byte FF contains a checksum of 0-BF to verify validity
(C0-FE are scratch or status registers, and FF contains the checksum)

some features are fudged a bit

"report light level" is used to control whether 0x86 reports are sent for status changes
which isn't exactly like a real US140

some features are unimplemented or only partially so:

o   transmission attempts (register 0x8E) are ignored for WUPB, i.e. each packet is only published once
		albeit with the transmission count and sequence bits set, so that if bridged to the line, the PIM will duplicate them accordingly
		be wary and do not program the WUPB device for three or four transmissions or the line will drown
		WiFi is reliable enough that retransmission shouldn't be necessary
o   many advanced options (status LED, local load, etc) are ignored
o		"last level" is not implemented
o		existing code only sends links. if direct device sends are desired, check for register 0x8E bit 7 (0=direct, 1=link)
o		some UPB commands (and any corresponding reports) are not implemented (i.e. 0x06 auto address)
o		0x07 generates an 87 report with the current error from EEPROM 0x1CC and 0x1CD *without clearing* the error
			unlike 0x10, this one can respond with an ack packet
			to clear the error, send 0x00 ping 
o   0x08 sets the error code in EEPROM 0x1CC and 0x1CD for testing hardware
o		0x0C erases any error code in EEPROM 0x1CC and 0x1CD
o		0x0E restarts. password is required as per spec.
o		0x12 dumps EEPROM to the serial monitor
o   0x1F resets the EEPROM (as with 0x0E, must include password to be sure)
o		some commands have custom reports
			0x13 returns 0x93 report (like 0x80 status but with no data). this may be sort of per spec

error checking is minimal, especially for length of some UPB commands

other functions are left as expansion for whatever intended purpose
eg rocker switch levels, rates, timers

minimal connection requirements are
o		momentary pushbutton for setup mode/reset
			press and hold for a bit to enter setup mode
			at which point the built-in ("blue") LED blinks
			press and hold for a bit again to exit
			press and holder longer, the LED will blink quicker
			a bit after that, the EEPROM will be reset
			to avoid the reset, let go while the LED is blinking
			the built-in LED blinks also flashes for WUPB data
o		"white" external LED is digital output
o		"green" external LED is analog output, 0-100%
o		"red" external LED blinks at varying rates for errors
			invalid commands are fast flashes, can be slower for motor stalls or other device-specific errors
*/


// this was a great idea for boards which require these LED's
// but eg the nightlight doesn't have any of them, and so doing this breaks it

uint8_t redLEDval=0;
uint8_t grnLEDval=0;
uint8_t bluLEDval=0;
uint8_t whtLEDval=0;

#include "ESP32UPBcore.h"

// US140T-specific values

// mS per step when blinking
uint32_t blinkrates[]={100,300,500,800,1000,1300,1600,1900,2100,2400,2700,2900,3200,3500,4700,4000};
bool blinkmode=false;
uint32_t lastoutputblink=0;
uint8_t outputblinkrate;

// mS per step when dimming
uint32_t faderates[]={0,4,8,17,25,33,50,100,150,300,600,1500,3000,4500,9000,18000};
// indexes into the above array
uint8_t currentfaderate=0;
uint8_t linkfaderate=0;
uint8_t globalfaderate=0;

uint32_t timers[]={1000,5000,30000,60000,120000,240000,600000,1200000,1800000,2400000,3000000,6000000,12000000,18000000,24000000,0};
// indexes into the above array
uint8_t currenttimer=15; // last one is "off"
uint8_t linktimer=0;
uint32_t linksend=0;

bool togglemode=false;
uint32_t lasttoggle=0;
uint8_t togglerate;
uint8_t togglecount;
uint8_t togglemax;

uint32_t lastdim=0;
bool dimming=false;
uint8_t targetlevel;

String timerstring[16];

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// prototypes

void clearstorederror(void); // in ESP32UPBerrors.h
void seterror(uint16_t errorlength, uint8_t errornumber); // in ESP32UPBerrors.h
void heartbeat(uint8_t who); // in ESP32core.h
void setdevicestate(uint8_t level); // here
void device_specific_setoutput(uint8_t dimlevel); // in caller
void reportstatus(uint8_t who); // in caller
void device_generic_command(uint8_t UPBdata[]); // here

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32US140Tsetup()
{
	UPBasib();

  EEPROM.begin(512);

  #if (DEBUG_LEVEL > 1) 
		dumpeeprom();
	#endif

	// these must be above the EEPROM check or else the check can't blink them
	#ifdef BUTTON_PIN
		pinMode(BUTTON_PIN, INPUT_PULLUP);
	#endif
	
	#ifdef BLUE_LED_PIN
		pinMode(BLUE_LED_PIN, OUTPUT);
	#endif

	#ifdef RED_LED_PIN
		pinMode(RED_LED_PIN, OUTPUT);
	#endif

	#ifdef WHITE_LED_PIN
		pinMode(WHITE_LED_PIN, OUTPUT);
	#endif

	#ifdef GREEN_LED_PIN
		// green LED is pin as specified, 5kHz, 8 bit resolution, pin as specified 
		ledcAttach(GREEN_LED_PIN, 5000, 8);
	#endif

	#ifdef ARDUINO_ESP32S3_DEV
		onboardLED.begin();
		onboardLED.setBrightness(BRIGHTNESS);
	#endif

	setLEDs();

  if (EEPROM.read(0xFF) == geteepromchecksum())
  {
			Serial.println("EEPROM valid");
  }
	else
	{
    Serial.println("EEPROM invalid");
	}

	#ifdef BUTTON_PIN
		if ((EEPROM.read(0xFF) != geteepromchecksum()) || (digitalRead(BUTTON_PIN)==LOW))
		{
			uint32_t lastchoice=millis();
			
			Serial.print("Do you want to reset the EEPROM?");
			
			uint32_t lastblink=millis();
			
			// must pick one or the other
			bool yesno=false;
			while (yesno == false)
			{
				// fast blink white and red as indication of corrupt EEPROM
				if ((millis()-lastblink) > 250)
				{
					redLEDval=!redLEDval;
					whtLEDval=!whtLEDval;
					setLEDs();

					lastblink=millis();
				}
				
				if ((millis()-lastchoice) > 10000)
				{
					Serial.println("\nChoice timeout");
					yesno=true;
				}
				
				if (Serial.available())
				{
					char thing=Serial.read();
					if ((thing=='Y') || (thing=='y'))
					{
						Serial.println("\nReset EEPROM");
						yesno=true;
						reseteeprom(0x00040022);
					}
					
					if ((thing=='N') || (thing=='n'))
					{
						Serial.println("\nDon't touch EEPROM");
						yesno=true;
					}
				}
			}
		}
	#endif

	timerstring[0]="1 second";
	timerstring[1]="5 seconds";
	timerstring[2]="30 seconds";
	timerstring[3]="1 minute";
	timerstring[4]="2 minutes";
	timerstring[5]="4 minutes";
	timerstring[6]="10 minutes";
	timerstring[7]="20 minutes";
	timerstring[8]="30 minutes";
	timerstring[9]="40 minutes";
	timerstring[10]="50 minutes";
	timerstring[11]="1 hour";
	timerstring[12]="2 hours";
	timerstring[13]="3 hours";
	timerstring[14]="4 hours";
	timerstring[15]="off";

	networkid=EEPROM.read(0);
  deviceid=EEPROM.read(1);
  globalfaderate=EEPROM.read(0x8D) & 0x0F;
  currentfaderate=globalfaderate;

  #if (DEBUG_LEVEL > 1)
    Serial.print("Dimming ");
    if ((EEPROM.read(0x8D) & 0x80) == 0x80) {Serial.println("enabled");}
    else {Serial.println("disabled");}

    Serial.printf("Global fade rate: %d\n",globalfaderate);
  #endif

	thisIP=deviceid;
	ESP32UPBsetup();

  refreshid();

	heartbeat(HOUSEMGR_PIM_ADDR);
	reportstatus(HOUSEMGR_PIM_ADDR);

  device_specific_setoutput(EEPROM.read(0xF9));
	// announce ourselves to the world
	if ((EEPROM.read(0x8D) & 0x10) == 0x10) {reportstatus(255);}
	
	if (EEPROM.read(0xFF) != geteepromchecksum())
	{
		Serial.println("EEPROM checksum failure");
		seterror(75,105);
	}
}
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32US140Tloop()
{
  if (blinkmode)
  {
    if ((millis() - lastoutputblink) > blinkrates[outputblinkrate])
    {
      uint8_t regF9=EEPROM.read(0xF9);
      if (regF9==0)
      {
        setdevicestate(100);
      }
      else
      {
        setdevicestate((uint8_t) 0); // force cast
      }
      lastoutputblink=millis();
    }
  }

  if (togglemode)
  {
    if ((millis() - lasttoggle) > blinkrates[togglerate])
    {
      uint8_t regF9=EEPROM.read(0xF9);
      if (regF9==0)
      {
        setdevicestate(100);
        togglecount++;
      }
      else
      {
        setdevicestate((uint8_t) 0); // force cast
      }

      lasttoggle=millis();

      if (togglecount==togglemax)
      {
        togglecount=0;
        togglemode=false;
        setdevicestate((uint8_t) 0); // force cast
      }
    }
  }

  if (timers[currenttimer])
  {
    if ((millis()-linksend) > timers[currenttimer])
    {
      Serial.printf("Timer off after %d\n",timers[currenttimer]/1000);
      currenttimer=15; // last one is "off"
      blinkmode=false;
      togglemode=false;
      setdevicestate((uint8_t) 0); // force cast
    }
  }

  if ((dimming) && ((millis()-lastdim) > faderates[currentfaderate]))
  {
    uint8_t regF9=EEPROM.read(0xF9);
    if (regF9 < targetlevel)
    {
      EEPROM.write(0xF9, regF9+1);
      commitflag=true;
      
      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade up: %d\n",regF9+1);
			#endif
      device_specific_setoutput(regF9+1);
    }
    else if (regF9 > targetlevel)
    {
      EEPROM.write(0xF9, regF9-1);
      commitflag=true;

      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade down: %d\n",regF9-1);
			#endif
      device_specific_setoutput(regF9-1);
    }
    else
    {
      #if (DEBUG_LEVEL > 1) 
				Serial.println("Fade done");
			#endif
      device_specific_setoutput(targetlevel);
      dimming=false;
      if ((EEPROM.read(0x8D) & 0x10) == 0x10) {reportstatus(255);}
    }
    lastdim=millis();
  }

	ESP32UPBloop();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// target level 0-100

void setdevicestate(uint8_t level)
{
	constrain(level,0,100);
	
  // blink and toggle commands snap on even if the device is set to fade
  bool fadeoverride=togglemode || blinkmode;
  
  // if dimming, let the state machine send a status report when done or stopped
  // currentfaderate 0 is snap
  if (((EEPROM.read(0x8D) & 0x80) == 0x80) && (!fadeoverride) && (currentfaderate))
  {
    #if (DEBUG_LEVEL > 0)
      Serial.printf("Fade to level %d%% using fade rate %d\n",level,currentfaderate);
    #endif

    dimming=true;
    targetlevel=level;
  }
  else // when snapping, send a status report
  {
    #if (DEBUG_LEVEL > 0)
      if (fadeoverride) {Serial.print("Override snap");}
      else {Serial.print("Snap");}
      Serial.printf(" to level %d%%\n",level);
    #endif
    EEPROM.write(0xF9, level);
    commitflag=true;
    device_specific_setoutput(level);
    if ((EEPROM.read(0x8D) & 0x10) == 0x10) {reportstatus(255);}
  }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void device_generic_command(uint8_t UPBdata[])
{
	uint8_t ct1=UPBdata[0];
	uint8_t ct2=UPBdata[1];
	uint8_t nid=UPBdata[2];
	uint8_t dst=UPBdata[3];
	uint8_t src=UPBdata[4];
	uint8_t msg=UPBdata[5];

	bool linkflag=UPBdata[0]>>7;
	uint8_t linknum;
	uint8_t linklevel=100;

	// device addressing -- just compare to ID
	if ((dst == deviceid) && (linkflag == 0))
	{
		#if (DEBUG_LEVEL > 1)
			Serial.printf("Addressed as device %d\n",dst);
		#endif
		addressed=true;
	}

	// destination 0 is broadcast, all devices listen
	if ((dst == 0) && (linkflag == 0))
	{
		#if (DEBUG_LEVEL > 1) 
			Serial.println("Addressed via broadcast");
		#endif
		addressed=true;
	}

	// link addressing -- must compare to table
	
	if (linkflag==1)
	{
		for (uint8_t i=0; i<16; i++)
		{
			linknum=EEPROM.read(0x40+i*3);
			if ((dst==linknum) && (linknum < 255))
			{
				addressed=true;
				linklevel=EEPROM.read(0x41+i*3);
				linkfaderate=EEPROM.read(0x42+i*3) & 0xF;
				linktimer=EEPROM.read(0x42+i*3) >> 4;
				if (linklevel > 100) {linklevel=100;}
				#if (DEBUG_LEVEL > 1)
					Serial.printf("Link L%d level %d fade rate %d time %d\n",linknum,linklevel,linkfaderate,linktimer);
				#endif

				// fade rate can be either default (global), or per-link
				if (linkfaderate==0xF) {currentfaderate=globalfaderate;}
				else {currentfaderate=linkfaderate;}

				// this takes advantage of "off" being the last in the array, set to 0
				currenttimer=linktimer;
				linksend=millis();
			}
		}
	}

	if (addressed) // That's us
	{
		uint8_t optbytes=(UPBdata[0] & 0b00011111)-7;
		
		// activate
		if (msg == 0x20) 
		{
			validcommand=true;
			clearstorederror();
			blinkmode=false; togglemode=false;
			#if (DEBUG_LEVEL > 1) 
				Serial.println("Activate");
			#endif
			if (linkflag) {setdevicestate(linklevel);} else {setdevicestate(100);}
			return;
		}
		
		// deactivate
		if (msg == 0x21) 
		{
			validcommand=true;
			clearstorederror();
			blinkmode=false; togglemode=false;
			#if (DEBUG_LEVEL > 1) 
				Serial.println("Deactivate");
			#endif
			setdevicestate((uint8_t) 0); // force cast
			return;
		}

		// go to level
		if (msg == 0x22) 
		{
			validcommand=true;
			clearstorederror();
			blinkmode=false; togglemode=false;
			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Go to level %d%%\n",UPBdata[6]);
			#endif
			if (optbytes == 2)
			{
				currentfaderate=UPBdata[7] & 0x0F;
				#if (DEBUG_LEVEL > 1)
					Serial.printf("Using fade rate %d\n",currentfaderate);
				#endif
			}
			else // if direct, don't use link fade rate
			{
				if (!linkflag) {currentfaderate=globalfaderate;}
			}
			setdevicestate(UPBdata[6]);
			return;
		}

		// fade start
		if (msg == 0x23) 
		{
			validcommand=true;
			clearstorederror();
			blinkmode=false; togglemode=false;
			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade start to level %d%%\n",UPBdata[6]); 
			#endif
				if (optbytes == 2)
			{
				currentfaderate=UPBdata[7] & 0x0F;
				#if (DEBUG_LEVEL > 1) 
					Serial.printf("Using fade rate %d\n",currentfaderate);
				#endif
			}
			else
			{
				currentfaderate=globalfaderate;
			}
			setdevicestate(UPBdata[6]);
			return;
		}

		// fade stop 
		if (msg == 0x24) 
		{
			validcommand=true;
			clearstorederror();
			dimming=false;
			blinkmode=false;
			togglemode=false;
			#if (DEBUG_LEVEL > 1)
				Serial.println("Fade stop");
			#endif
			if ((EEPROM.read(0x8D) & 0x10) == 0x10) {reportstatus(src);}
			return;
		}

		// blink
		if ((msg == 0x25) && (optbytes==1))
		{
			validcommand=true;
			clearstorederror();
			#if (DEBUG_LEVEL > 1) 
				Serial.println("Blink");
			#endif
			blinkmode=true;
			outputblinkrate=(UPBdata[6]>>4) & 0x0F;
			return;
		}

		// toggle
		if (msg == 0x27) 
		{
			validcommand=true;
			clearstorederror();
			#if (DEBUG_LEVEL > 1) 
				Serial.println("Toggle");
			#endif
			setdevicestate((uint8_t) 0);
			togglemode=true;
			togglemax=UPBdata[6];
			if (optbytes == 2)
			{
				togglerate=(UPBdata[7]>>4) & 0x0F;
			}
			else
			{
				togglerate=2;
			}
			return;
		}
	}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String timerOptions(uint8_t timer)
{
  String retval;
  retval="";
  
  for (uint8_t i=0; i<16; i++)
  {
    retval=retval+"<option";
    if (i==timer)
    {
      retval=retval+" selected";
    }
    String mask=intToBinString(16*i);
    mask.setCharAt(4,'x');
    mask.setCharAt(5,'x');
    mask.setCharAt(6,'x');
    mask.setCharAt(7,'x');
    retval=retval+" value=\""+mask+"\">"+timerstring[i]+"<br>\n";
  }
  return retval;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

