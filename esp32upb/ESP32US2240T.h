#define ESP32US2240Tversion 300

/*
Universal Powerline Bus emulation of Simply Automated US22-40T dimmer switch (firmware 01.20)

forked from ESP32US140T.h 11 september 2023
*/

uint8_t redLEDval=0;
uint8_t grnLEDval=0;
uint8_t secLEDval=0;
uint8_t bluLEDval=0;
uint8_t whtLEDval=0;

#include "ESP32UPBcore.h"

// US2240T-specific values

// mS per step when blinking
uint32_t blinkrates[]={100,300,500,800,1000,1300,1600,1900,2100,2400,2700,2900,3200,3500,4700,4000};
bool blinkmode1=false;
bool blinkmode2=false;
uint32_t lastoutputblink1=0;
uint32_t lastoutputblink2=0;
uint8_t outputblinkrate1;
uint8_t outputblinkrate2;

// mS per step when dimming
uint32_t faderates[]={0,4,8,17,25,33,50,100,150,300,600,1500,3000,4500,9000,18000};
// indexes into the above array
uint8_t currentfaderate1=0;
uint8_t currentfaderate2=0;
uint8_t linkfaderate=0;
uint8_t globalfaderate1=0;
uint8_t globalfaderate2=0;

uint32_t timers[]={1000,5000,30000,60000,120000,240000,600000,1200000,1800000,2400000,3000000,6000000,12000000,18000000,24000000,0};
// indexes into the above array
uint8_t currenttimer1=15; // last one is "off"
uint8_t currenttimer2=15; // last one is "off"
uint8_t linktimer=0;
uint32_t linksend1=0;
uint32_t linksend2=0;

bool togglemode1=false;
bool togglemode2=false;
uint32_t lasttoggle1=0;
uint32_t lasttoggle2=0;
uint8_t togglerate1;
uint8_t togglerate2;
uint8_t togglecount1;
uint8_t togglecount2;
uint8_t togglemax1;
uint8_t togglemax2;

uint32_t lastdim1=0;
uint32_t lastdim2=0;
bool dimming1=false;
bool dimming2=false;
uint8_t targetlevel1;
uint8_t targetlevel2;

String timerstring[16];

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// prototypes

void clearstorederror(void); // in ESP32UPBerrors.h
void seterror(uint16_t errorlength, uint8_t errornumber); // in ESP32UPBerrors.h
void heartbeat(uint8_t who); // in ESP32core.h
void setdevicestate(uint8_t dimlevel, uint8_t channel); // here
void device_specific_setoutput(uint8_t dimlevel, uint8_t outputchannel); // in caller
void reportstatus(uint8_t who); // in caller
void device_generic_command(uint8_t UPBdata[]); // here

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32US2240Tsetup()
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

	#ifdef SECOND_LED_PIN
		// second LED is pin as specified, 5kHz, 8 bit resolution, pin as specified 
		ledcAttach(SECOND_LED_PIN, 5000, 8);
		Serial.printf("Second LED on %d\n",SECOND_LED_PIN);
	#endif

	#ifdef ARDUINO_ESP32S3_DEV
		onboardLED.begin();
		onboardLED.setBrightness(BRIGHTNESS);
	#endif

	setLEDs();

  if (EEPROM.read(0xFF) == geteepromchecksum())
  {
    #if (DEBUG_LEVEL > 1) 
			Serial.println("EEPROM valid");
		#endif
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
						reseteeprom(0x0004003E);

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
  globalfaderate1=EEPROM.read(0x88) & 0x0F;
  globalfaderate2=EEPROM.read(0x89) & 0x0F;
  currentfaderate1=globalfaderate1;
  currentfaderate2=globalfaderate2;

  #if (DEBUG_LEVEL > 1)
    Serial.print("Channel 1 dimming ");
    if ((EEPROM.read(0x88) & 0x80) == 0x80) {Serial.println("enabled");}
    else {Serial.println("disabled");}

    Serial.printf("Global fade rate 1: %d\n",globalfaderate1);
    Serial.print("Channel 2 dimming ");
    if ((EEPROM.read(0x89) & 0x80) == 0x80) {Serial.println("enabled");}
    else {Serial.println("disabled");}

    Serial.printf("Global fade rate 2: %d\n",globalfaderate2);
  #endif

	thisIP=deviceid;
	ESP32UPBsetup();

	refreshid();

	heartbeat(HOUSEMGR_PIM_ADDR);
	reportstatus(HOUSEMGR_PIM_ADDR);

	device_specific_setoutput(EEPROM.read(0xF6),1);
	device_specific_setoutput(EEPROM.read(0xF7),2);
	// announce ourselves to the world
	if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(255);}

	if (EEPROM.read(0xFF) != geteepromchecksum())
	{
		Serial.println("EEPROM checksum failure");
		seterror(75,105);
	}

}
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32US2240Tloop()
{
	uint8_t regF6=EEPROM.read(0xF6);
	uint8_t regF7=EEPROM.read(0xF7);

  if (blinkmode1)
  {
    if ((millis() - lastoutputblink1) > blinkrates[outputblinkrate1])
    {
      if (regF6==0)
      {
        setdevicestate(100,1);
      }
      else
      {
        setdevicestate((uint8_t) 0,1); // force cast
      }
      lastoutputblink1=millis();
    }
  }
	
  if (blinkmode2)
  {
    if ((millis() - lastoutputblink2) > blinkrates[outputblinkrate2])
    {
      if (regF7==0)
      {
        setdevicestate(100,2);
      }
      else
      {
        setdevicestate((uint8_t) 0,2); // force cast
      }
      lastoutputblink2=millis();
    }
  }

  if (togglemode1)
  {
    if ((millis() - lasttoggle1) > blinkrates[togglerate1])
    {
      if (regF6==0)
      {
        setdevicestate(100,1);
        togglecount1++;
      }
      else
      {
        setdevicestate((uint8_t) 0,1); // force cast
      }

      lasttoggle1=millis();

      if (togglecount1==togglemax1) 
      {
        togglecount1=0;
        togglemode1=false;
        setdevicestate((uint8_t) 0,1); // force cast
      }
    }
  }

  if (togglemode2)
  {
    if ((millis() - lasttoggle2) > blinkrates[togglerate2])
    {
      if (regF7==0)
      {
        setdevicestate(100,2);
        togglecount2++;
      }
      else
      {
        setdevicestate((uint8_t) 0,2); // force cast
      }

      lasttoggle2=millis();

      if (togglecount2==togglemax2)
      {
        togglecount2=0;
        togglemode2=false;
        setdevicestate((uint8_t) 0,2); // force cast
      }
    }
  }

  if (timers[currenttimer1])
  {
    if ((millis()-linksend1) > timers[currenttimer1])
    {
      Serial.printf("Timer 1 off after %d\n",timers[currenttimer1]/1000);
      currenttimer1=15; // last one is "off"
      blinkmode1=false;
      togglemode1=false;
      setdevicestate((uint8_t) 0,1); // force cast
    }
  }

  if (timers[currenttimer2])
  {
    if ((millis()-linksend2) > timers[currenttimer2])
    {
      Serial.printf("Timer 2 off after %d\n",timers[currenttimer2]/1000);
      currenttimer2=15; // last one is "off"
      blinkmode2=false;
      togglemode2=false;
      setdevicestate((uint8_t) 0,2); // force cast
    }
  }

  if ((dimming1) && ((millis()-lastdim1) > faderates[currentfaderate1]))
  {
    if (regF6 < targetlevel1)
    {
      EEPROM.write(0xF6, regF6+1);
      commitflag=true;
      
      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade channel 1 up: %d\n",regF6+1);
			#endif
      device_specific_setoutput(regF6+1,1);
    }
    else if (regF6 > targetlevel1)
    {
      EEPROM.write(0xF6, regF6-1);
      commitflag=true;

      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade channel 1 down: %d\n",regF6-1);
			#endif
      device_specific_setoutput(regF6-1, 1);
    }
    else
    {
      #if (DEBUG_LEVEL > 1) 
				Serial.println("Fade channel 1 done");
			#endif
      device_specific_setoutput(targetlevel1, 1);
      dimming1=false;
      if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(255);}
    }
    lastdim1=millis();
  }

  if ((dimming2) && ((millis()-lastdim2) > faderates[currentfaderate2]))
  {
    if (regF7 < targetlevel2)
    {
      EEPROM.write(0xF7, regF7+1);
      commitflag=true;
      
      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade channel 2 up: %d\n",regF7+1);
			#endif
      device_specific_setoutput(regF7+1,2);
    }
    else if (regF7 > targetlevel2)
    {
      EEPROM.write(0xF7, regF7-1);
      commitflag=true;

      #if (DEBUG_LEVEL > 1) 
				Serial.printf("Fade channel 2 down: %d\n",regF7-1);
			#endif
      device_specific_setoutput(regF7-1,2);
    }
    else
    {
      #if (DEBUG_LEVEL > 1) 
				Serial.println("Fade channel 2 done");
			#endif
      device_specific_setoutput(targetlevel2,2);
      dimming2=false;
      if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(255);}
    }
    lastdim2=millis();
  }

	ESP32UPBloop();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// target level 0-100
// channel 0 both, 1 first, 2 second

void setdevicestate(uint8_t dimlevel, uint8_t channel)
{
	constrain(dimlevel,0,100);
	
	if ((channel==1) || (channel==0))
	{
		// blink and toggle commands snap on even if the device is set to fade
		bool fadeoverride1=togglemode1 || blinkmode1;
		
		// if dimming, let the state machine send a status report when done or stopped
		// faderate 0 is snap
		if (((EEPROM.read(0x88) & 0x80) == 0x80) && (!fadeoverride1) && (currentfaderate1))
		{
			#if (DEBUG_LEVEL > 0)
				Serial.printf("SDS fade channel 1 to level %d%% using fade rate %d\n",dimlevel,currentfaderate1);
			#endif

			dimming1=true;
			targetlevel1=dimlevel;
		}
		else // when snapping, send a status report
		{
			#if (DEBUG_LEVEL > 0)
				if (fadeoverride1) {Serial.print("Override snap");}
				else {Serial.print("Snap");}
				Serial.printf(" channel 1 to level %d%%\n",dimlevel);
			#endif
			EEPROM.write(0xF6, dimlevel);
			commitflag=true;
			device_specific_setoutput(dimlevel, 1);
			if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(255);}
		}
	}
	
	if ((channel==2) || (channel==0))
	{
		// blink and toggle commands snap on even if the device is set to fade
		bool fadeoverride2=togglemode2 || blinkmode2;
		
		// if dimming, let the state machine send a status report when done or stopped
		// faderate 0 is snap
		if (((EEPROM.read(0x89) & 0x80) == 0x80) && (!fadeoverride2) && (currentfaderate2))
		{
			#if (DEBUG_LEVEL > 0)
				Serial.printf("SDS fade channel 2 to level %d%% using fade rate %d\n",dimlevel,currentfaderate2);
			#endif

			dimming2=true;
			targetlevel2=dimlevel;
		}
		else // when snapping, send a status report
		{
			#if (DEBUG_LEVEL > 0)
				if (fadeoverride2) {Serial.print("Override snap");}
				else {Serial.print("Snap");}
				Serial.printf(" channel 2 to level %d%%\n",dimlevel);
			#endif
			EEPROM.write(0xF7, dimlevel);
			commitflag=true;
			device_specific_setoutput(dimlevel, 2);
			if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(255);}
		}
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

	// there is a minor issue here, in that an identical link in the tables for BOTH channels
	// will only take effect for channel 2. should that ever be a desired feature, TBD to fix
	// move all of "if (addressed)..." into a separate function, then do it twice, once for each channel?
	// two separate flags addressed1, addressed2?
	
	uint8_t channel=0;
	
	// link addressing -- must compare to tables
	
	if (linkflag==1)
	{
		for (uint8_t i=0; i<16; i++)
		{
			linknum=EEPROM.read(0x96+i*3);
			if ((dst==linknum) && (linknum < 255))
			{
				addressed=true;
				channel=1;
				linklevel=EEPROM.read(0x97+i*3);
				linkfaderate=EEPROM.read(0x98+i*3) & 0xF;
				linktimer=EEPROM.read(0x98+i*3) >> 4;
				if (linklevel > 100) {linklevel=100;}
				#if (DEBUG_LEVEL > 1)
					Serial.printf("Channel 1 Link L%d level %d fade rate %d time %d\n",linknum,linklevel,linkfaderate,linktimer);
				#endif

				// fade rate can be either default (global), or per-link
				if (linkfaderate==0xF) {currentfaderate1=globalfaderate1;}
				else {currentfaderate1=linkfaderate;}

				// this takes advantage of "off" being the last in the array, set to 0
				currenttimer1=linktimer;
				linksend1=millis();
			}
		}

		for (uint8_t i=0; i<16; i++)
		{
			linknum=EEPROM.read(0xC6+i*3);
			if ((dst==linknum) && (linknum < 255))
			{
				addressed=true;
				channel=2;
				linklevel=EEPROM.read(0xC7+i*3);
				linkfaderate=EEPROM.read(0xC8+i*3) & 0xF;
				linktimer=EEPROM.read(0xC8+i*3) >> 4;
				if (linklevel > 100) {linklevel=100;}
				#if (DEBUG_LEVEL > 1)
					Serial.printf("Channel 2 Link L%d level %d fade rate %d time %d\n",linknum,linklevel,linkfaderate,linktimer);
				#endif

				// fade rate can be either default (global), or per-link
				if (linkfaderate==0xF) {currentfaderate2=globalfaderate2;}
				else {currentfaderate2=linkfaderate;}

				// this takes advantage of "off" being the last in the array, set to 0
				currenttimer2=linktimer;
				linksend2=millis();
			}
		}
	}

	if (addressed) // That's us
	{

		uint8_t optbytes=(UPBdata[0] & 0b00011111)-7;
		
		Serial.printf("Opt bytes %d\n",optbytes);
		
		// activate
		if (msg == 0x20) 
		{
			validcommand=true;
			clearstorederror();

			if (optbytes==3) {channel=UPBdata[8];	Serial.printf("Using channel %d\n",channel);}	else {Serial.println("Using both channels"); channel=0;}

			if ((channel==0) || (channel==1)) {blinkmode1=false; togglemode1=false;}
			if ((channel==0) || (channel==2)) {blinkmode2=false; togglemode2=false;}

			#if (DEBUG_LEVEL > 1) 
				Serial.println("Activate");
			#endif
			if (linkflag) {setdevicestate(linklevel,channel);} else {setdevicestate(100,channel);}
			return;
		}
		
		// deactivate
		if (msg == 0x21) 
		{
			validcommand=true;
			clearstorederror();
			
			if (optbytes==3) {channel=UPBdata[8];	Serial.printf("Using channel %d\n",channel);}	else {Serial.println("Using both channels"); channel=0;}

			if ((channel==0) || (channel==1)) {blinkmode1=false; togglemode1=false;}
			if ((channel==0) || (channel==2)) {blinkmode2=false; togglemode2=false;}
				
			#if (DEBUG_LEVEL > 1) 
				Serial.println("Deactivate");
			#endif
			setdevicestate((uint8_t) 0,channel); // force cast
			return;
		}

		// go to level
		if (msg == 0x22) 
		{
			validcommand=true;
			clearstorederror();

			if (optbytes==3)
			{
				channel=UPBdata[8]; 
				Serial.printf("Using channel %d\n",channel);
//				Serial.printf("OB3 Should use fade rate %d\n",UPBdata[7]);
				if (channel==1)
				{
					currentfaderate1=UPBdata[7]&0x0f;
				}
				if (channel==2)
				{
					currentfaderate2=UPBdata[7]&0x0f;
				}
			}
			else
			{
				Serial.println("Using both channels"); channel=0;
				if (optbytes == 2)
				{
//					Serial.printf("OB2 Should use fade rate %d\n",UPBdata[7]);
					currentfaderate1=UPBdata[7]&0x0f;
					currentfaderate2=UPBdata[7]&0x0f;
				}
			}

			#if (DEBUG_LEVEL > 1)
				Serial.printf("Floop channel 1 fade current %d global %d\n",currentfaderate1,globalfaderate1);
				Serial.printf("Floop channel 2 fade current %d global %d\n",currentfaderate2,globalfaderate2);
			#endif

			if ((channel==0) || (channel==1)) {blinkmode1=false; togglemode1=false;}
			if ((channel==0) || (channel==2)) {blinkmode2=false; togglemode2=false;}

			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Channel %d go to level %d%%\n",channel,UPBdata[6]);
			#endif
			if (optbytes > 1)
			{
				if ((channel==0) || (channel==1))
				{
					currentfaderate1=UPBdata[7] & 0x0f;
					// default
					if (currentfaderate1 == 0x0f)
					{
						currentfaderate1=EEPROM.read(0x88) & 0x0F;
					}
					#if (DEBUG_LEVEL > -1)
						Serial.printf("Channel 1 using current fade rate %d\n",currentfaderate1);
					#endif
				}
				if ((channel==0) || (channel==2))
				{
					currentfaderate2=UPBdata[7] & 0x0f;
					// default
					if (currentfaderate2== 0x0f)
					{
						currentfaderate2=EEPROM.read(0x89) & 0x0F;
					}

					#if (DEBUG_LEVEL > -1)
						Serial.printf("Channel 2 using current fade rate %d\n",currentfaderate2);
					#endif
				}
			}
			else // if direct, don't use link fade rate
			{
				if (!linkflag)
				{
					if ((channel==0) || (channel==1))
					{
						currentfaderate1=globalfaderate1;
						#if (DEBUG_LEVEL > 1)
							Serial.printf("Channel 1 using global fade rate %d\n",currentfaderate1);
						#endif
					}
					if ((channel==0) || (channel==2))
					{
						currentfaderate2=globalfaderate2;
						#if (DEBUG_LEVEL > 1)
							Serial.printf("Channel 2 using global fade rate %d\n",currentfaderate2);
						#endif
					}
				} 
			}
			setdevicestate(UPBdata[6],channel);
			return;
		}

		// fade start
		if (msg == 0x23) 
		{
			validcommand=true;
			clearstorederror();
			
			if ((channel==0) || (channel==1)) {blinkmode1=false; togglemode1=false;}
			if ((channel==0) || (channel==2)) {blinkmode2=false; togglemode2=false;}

			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Channel %d fade start to level %d%%\n",channel,UPBdata[6]); 
			#endif
			
			if (optbytes > 1)
			{
				if ((channel==0) || (channel==1))
				{
					currentfaderate1=UPBdata[7] & 0x0F;
					#if (DEBUG_LEVEL > 1)
						Serial.printf("Channel 1 using fade rate %d\n",currentfaderate1);
					#endif
				}
				if ((channel==0) || (channel==2))
				{
					currentfaderate2=UPBdata[7] & 0x0F;
					#if (DEBUG_LEVEL > 1)
						Serial.printf("Channel 2 using fade rate %d\n",currentfaderate2);
					#endif
				}
			}
			else
			{
					if ((channel==0) || (channel==1))
					{
						currentfaderate1=globalfaderate1;
						#if (DEBUG_LEVEL > 1)
							Serial.printf("Channel 1 using fade rate %d\n",currentfaderate1);
						#endif
					}
					if ((channel==0) || (channel==2))
					{
						currentfaderate2=globalfaderate2;
						#if (DEBUG_LEVEL > 1)
							Serial.printf("Channel 2 using fade rate %d\n",currentfaderate2);
						#endif
					}
			}
			setdevicestate(UPBdata[6],channel);
			return;
		}

		// fade stop 
		if (msg == 0x24) 
		{
			validcommand=true;
			clearstorederror();
			
			if ((channel==0) || (channel==1)) {dimming1=false; blinkmode1=false; togglemode1=false;}
			if ((channel==0) || (channel==2)) {dimming2=false; blinkmode2=false; togglemode2=false;}

			#if (DEBUG_LEVEL > 1)
				Serial.printf("Channel %d fade stop\n",channel);
			#endif
			if ((EEPROM.read(0x90) & 0x10) == 0x10) {reportstatus(src);}
			return;
		}

		// blink
		if ((msg == 0x25) && (optbytes==1))
		{
			validcommand=true;
			clearstorederror();
			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Channel %d blink\n");
			#endif
			
			if ((channel==0) || (channel==1)) {blinkmode1=true; outputblinkrate1=(UPBdata[6]>>4) & 0x0F;}
			if ((channel==0) || (channel==2)) {blinkmode2=true; outputblinkrate2=(UPBdata[6]>>4) & 0x0F;}
			
			return;
		}

		// toggle
		if (msg == 0x27) 
		{
			validcommand=true;
			clearstorederror();
			#if (DEBUG_LEVEL > 1) 
				Serial.printf("Channel %d toggle\n",channel);
			#endif
			setdevicestate((uint8_t) 0,channel);
			
			if ((channel==0) || (channel==1))
			{
				togglemode1=true; togglemax1=UPBdata[6];
				if (optbytes == 2) {togglerate1=(UPBdata[7]>>4) & 0x0F;}
				else {togglerate1=2;}
			}
			if ((channel==0) || (channel==2))
			{
				togglemode2=true; togglemax2=UPBdata[6];
				if (optbytes == 2) {togglerate2=(UPBdata[7]>>4) & 0x0F;}
				else {togglerate2=2;}
			}
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
