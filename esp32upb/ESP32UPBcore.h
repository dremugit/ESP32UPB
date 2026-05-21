#define ESP32UPBcoreversion 307

/*
ESP32 emulation of WUPB devices, over MQTT
See ESP32UPBUS140T.h and ESP32US2240T.h for device specific information

This file  covers most of the basic command set
of note is that the high bit of the second byte in the control word ("CTL2") is set high
this is against the UPB spec, but is very useful: the bridge sketch then knows which packets came from the line and which from WUPB but were send to the line
it can then filter off packets with this bit set and not RE-publish them, resulting in an infinite loop
this obviates putting the sender into the MQTT packet or having the bridge track packets it's already sent or other complicated 'solutions'

WUPB devices send  an MDID==0x80 packet to acknowledge receipt like Gen2 UPB devices, with CTL2 set to match the request (any/all of ack packet, ack pulse, ID pulse)
the PIM emulator can then use the MDID==0x80 packet to determine how to make the WUPB device appear to UPStart, including generating ID pulses to add to the network
*/

#define HOUSEMGR_PIM_ADDR                       0xFB					

#include <WiFi.h>

#include "ESP32UPBdisplay.h"
#include "ESP32UPBsecrets.h"
#include "ESP32UPBeeprom.h"
#include "ESP32UPBerrors.h"

String firmware;
String firmwaredate;
String firmwaretime;

uint32_t lastnetworkcheck=0;
#define NETWORK_CHECK_SPACING      	5*60*1000	// check for network connection every five minutes

uint32_t networkreboot;
bool networkrebootflag=false;
#define NETWORK_REBOOT_AFTER				2*60*1000 // after network loss, reboot in two minutes. allows local function without excessive cycling

#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient mqttclient(espClient); 
uint32_t lastReconnectAttempt = 0;

// stuff for web server

#include "esp_task_wdt.h"
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer webserver(80);

void notFound(AsyncWebServerRequest *request)
{
    request->send(404, "text/plain", "Not found");
}

// stuff for updates

#include <HTTPClient.h>
#include <HTTPUpdate.h>
bool flashfirmware=false;
String firmwarefile="";
bool restartflag=false;

void update_progress(uint32_t cur, uint32_t total)
{
  Serial.printf("HTTP update process at %d of %d bytes\n", cur, total);
}

void update_error(uint16_t err) // not sure if 8 or 16 or what
{
  Serial.printf("HTTP update fatal error code %d\n", err);
}

// UPB stuff

bool refreshidflag=false;

uint8_t PIMdeviceid;
char ackpacket[17];

#define SETUP_BUTTON_HOLD     5000      // hold button a while to enter/exit setup mode
#define RESET_BUTTON_WARNING  15000     // "blue" LED will flash faster at this point, approaching the above
#define RESET_BUTTON_HOLD     20000     // hold button longer to reset EEPROM to default
#define SETUP_BLINK_SPACING   500       // mS between setup LED blinks
#define RESET_BLINK_SPACING   200       // mS between reset LED blinks
#define EEPROM_SPACING        1000      // mS between updates to EEPROM while in action
#define SETUP_MODE_TIMEOUT    300000    // mS after which setup mode is cancelled
																				// has to be long enough to allow UPStart to program

#define DEBOUNCE_DELAY        50        // mS to debounce switch

char hexchars[2];

uint8_t networkid, deviceid;
uint32_t setupmodestarted;
bool writeenable=false;
uint32_t writeenablestarted;

char netv[]="0123456789ABCDEF\x00";
char room[]="0123456789ABCDEF\x00";
char devn[]="0123456789ABCDEF\x00";

uint8_t bindata[128];

uint32_t lastUPBreceive=0;

bool addressed;
bool validcommand;
uint8_t UPBcmd=0;

uint8_t oldbutton=1, button=1; // 1 is NOT pressed
uint32_t lastbuttontime=0;
bool buttonstart=false;
bool buttonpressed=false;
uint8_t buttonstage=0;
uint16_t blueLEDspacing=0;
uint16_t whiteLEDspacing=0;
uint32_t buttondelta;
uint8_t buttonstate, oldbuttonstate;
uint32_t lastdebouncetime;

bool setupmode=false;
uint32_t lastblueblink=0;
uint32_t lastredblink=0;

// the ESP takes a performance hit when doing EEPROM writes, so the commits are spaced out some
// except when absolutely required
uint32_t lastcommit=0;

// outgoing queue to PIM
#define PIM_QUEUE_SIZE       32
char PIMqueue[PIM_QUEUE_SIZE][100];
uint32_t nextsendtime;
int qdisplayflag=-1;

bool PIMready=false;
uint32_t lastPIMready=0;

// wireless side

uint8_t thisIP;

// prototypes come right before functions but after all the variables they might use

#ifdef ESP32US2240Tversion
	void device_specific_setoutput(uint8_t dimlevel, uint8_t outputchannel); // in caller
#endif

#ifdef ESP32US140Tversion
	void device_specific_setoutput(uint8_t dimlevel); // in caller
#endif

bool device_specific_command(uint8_t UPBdata[]);
void handleEditSettings(AsyncWebServerRequest *request);
String device_specific_root(void);

// in ESP32US140T.h
void device_generic_command(uint8_t UPBdata[]);

// in ESP32UPBerrors.h
void seterror(uint16_t errorlength, uint8_t errornumber);
void clearstorederror(void);

// in ESP32UPBdisplay.h
uint8_t hexcharstobyte(char thing1, char thing2);
bool isprintable(char thing);

// in shade or whatever
void reportstatus(uint8_t who);

// here
void heartbeat(uint8_t who);
void processUPB(char UPBstring[]);
void displayUPB(char dir[], char UPBstring[]);
uint8_t UPBchecksum(char UPBcommand[]);
void initWiFi(void);
void ESP32UPBsetup(void);
void ESP32UPBloop(void);
uint8_t UPBcommandlength(char UPBcommand[]);
void refreshid();
uint16_t registerchecksum(uint8_t numreg);
void UPBsend(char txstring[]);
void sendbinstring(uint8_t binarydata[]);
void UPBasib(void);
void handleRoot(AsyncWebServerRequest *request);
void handleReboot(AsyncWebServerRequest *request);
void handleUpdate(AsyncWebServerRequest *request);
String UPBdevicename(void);
void handleSaveSettings(AsyncWebServerRequest *request);
void handleUPBcommand(AsyncWebServerRequest *request);
void handleSendThisCommand(AsyncWebServerRequest *request);
String noHTMLlink(void);
void mqttcallback(char* topic, byte* payload, uint16_t msglength);
bool reconnect();
void waitformqttconnect(uint32_t cycletime);
void handleDumpRegisters(AsyncWebServerRequest *request);
void handleEnterSetupMode(AsyncWebServerRequest *request);
void handleExitSetupMode(AsyncWebServerRequest *request);

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

uint8_t UPBchecksum(char UPBcommand[])
{
  // lower five bits of CTL1 are packet length
  int packetlength=hexcharstobyte(UPBcommand[0],UPBcommand[1]) & 0b00011111;

  uint32_t total=0;

  for (int i=0; i<(packetlength-1)*2; i+=2)
  {
    total+=hexcharstobyte(UPBcommand[i],UPBcommand[i+1]);
  }

  return (256-(total & 0xFF)) & 0xFF;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void initWiFi(void)
{
	bluLEDval=1;
	setLEDs();
	// Configures static IP address
	localip={192,168,239,thisIP};
	if (!WiFi.config(localip, gateway, netmask))
	{
		Serial.printf("WiFi failed to configure for 192.168.239.%d\n",thisIP);
		bluLEDval=0;
		setLEDs();
		seterror(100,100);
		return;
	}

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
	
	uint32_t wificonnecttime=millis();
	
	while (
	((millis()-wificonnecttime) < 5000) // 5sec window to start wifi
	&&
	(WiFi.status()!=WL_CONNECTED)
	)
	{
		yield();
	}

	if (WiFi.status()!=WL_CONNECTED)
  {
		Serial.printf("WiFi Failed!\n");
		bluLEDval=0;
		setLEDs();
		seterror(100,100);
		return;
  }

	
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("MAC address: ");
  Serial.println(WiFi.macAddress());
	bluLEDval=0;
	setLEDs();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32UPBsetup(void)
{
	Serial.print("Firmware file: "); Serial.println(firmware);

	initWiFi();
	
  webserver.on("/", HTTP_GET, handleRoot);
  webserver.on("/reboot", HTTP_GET, handleReboot);
  webserver.on("/update", HTTP_GET, handleUpdate);
  webserver.on("/edit", HTTP_GET, handleEditSettings);
  webserver.on("/upbcommand", HTTP_GET, handleUPBcommand);
  webserver.on("/entersetupmode", HTTP_GET, handleEnterSetupMode);
  webserver.on("/exitsetupmode", HTTP_GET, handleExitSetupMode);
  webserver.on("/sendthiscommand", HTTP_GET, handleSendThisCommand);
  webserver.on("/dumpregs", HTTP_GET, handleDumpRegisters);
  webserver.on("/save", HTTP_POST, handleSaveSettings); // note it's the one POST as it has too much to deal with
  webserver.onNotFound(notFound);

  webserver.begin();

  mqttclient.setServer(mqttserver, 1883);
  mqttclient.setCallback(mqttcallback);
	
	// give the MQTT loop a bit to spin up; otherwise, the first status publish(es) fail
//	waitformqttconnect(3000);

	
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void ESP32UPBloop(void)
{
	if (networkrebootflag)
	{
		if ((millis()-networkreboot) > 0)
		{
			#ifdef DISABLE_2240T_OUTPUT_FOR_UPDATE
				Serial.println("Turning off for reboot");
				device_specific_setoutput(0, 0);
			#endif
			#ifdef DISABLE_140T_OUTPUT_FOR_UPDATE
				Serial.println("Turning off for reboot");
				device_specific_setoutput(0);
			#endif
			
			networkrebootflag=false;
			Serial.println("Network failure reboot");
			ESP.restart();
		}
	}
		
  if (((millis() - lastnetworkcheck) > NETWORK_CHECK_SPACING) && (deviceid != 0xFE))
  {
    lastnetworkcheck=millis();
		#if DEBUG_LEVEL > 1
			Serial.println("Network check");
		#endif

		if (WiFi.isConnected())
		{
			if (EEPROM.read(0x1CE)==101)
			{
				Serial.println("Clearing WiFi disconnection error");
				clearstorederror();
				networkrebootflag=false;
			}

			waitformqttconnect(0);
			if (mqttclient.connected())
			{
				if (EEPROM.read(0x1CE)==103)
				{
					Serial.println("MQTT connection good, clearing disconnection error");
					clearstorederror();
					networkrebootflag=false;
				}
			}
			else
			{
				if (setupmode==false)
				{
					if (EEPROM.read(0x1CE)!=103)
					{
						Serial.println("MQTT disconnected");
						seterror(150,103);
						networkreboot=millis()+NETWORK_REBOOT_AFTER;
						networkrebootflag=true;
					}
				}
			}
			
		}
		else
		{
			if (EEPROM.read(0x1CE)!=101)
			{
				Serial.println("WiFi disconnected");
				seterror(150,101);
				networkreboot=millis()+NETWORK_REBOOT_AFTER;
				networkrebootflag=true;
			}
		}
  }

	
	// only write EEPROM changes occasionally
	if ((commitflag) && ((millis() - lastcommit) > EEPROM_SPACING))
	{
		seteepromchecksum();
		EEPROM.commit();
		lastcommit=millis();
		commitflag=false;
	}

	#ifdef BUTTON_PIN
		// debounce pushbutton

		buttonstate=digitalRead(BUTTON_PIN);

		// reset counter until reading stays stable for DEBOUNCE_DELAY mS
		if (buttonstate != oldbuttonstate) {lastdebouncetime=millis();}

		if ((millis() - lastdebouncetime) > DEBOUNCE_DELAY)
		{
			button=buttonstate;
			#if DEBUG_LEVEL > 1
				if (button != oldbutton)
				{
					Serial.printf("Button %d\n",button);
				}
			#endif
		}

		oldbuttonstate=buttonstate;

		// as it's a grounding switch on a pullup resistor
		// 0 is on and 1 is off

		if ((button==0) && (oldbutton==1))
		{
			if (buttonpressed==false)
			{
				#if DEBUG_LEVEL > 1
					Serial.println("Initial button press");
				#endif
				buttonpressed=true;
				lastbuttontime=millis();
			}
		}

		buttondelta=millis()-lastbuttontime;

		if ((button==1) && (oldbutton==0))
		{
			#if DEBUG_LEVEL > 1
				Serial.println("Button release = button stage 0");
			#endif

			if (buttonstage==1)
			{
				#if DEBUG_LEVEL > 1
					Serial.println("Short button press, toggle setup mode");
				#endif
				setupmode=!setupmode;

				if (setupmode)
				{
					Serial.println("Entering setup mode");
					blueLEDspacing=500;
					writeenable=true;
					writeenablestarted=millis();
					networkid=0x00; // broadcast network ID  
					deviceid=0xFE; // broadcast device ID
					setupmodestarted=millis();
				}
				else
				{
					Serial.println("Exiting setup mode");
					blueLEDspacing=0;
					writeenable=false;
					bluLEDval=0;
					setLEDs();
					networkid=EEPROM.read(0); // actual network ID  
					deviceid=EEPROM.read(1); // actual device ID
				}
			}

			if (buttonstage==2)
			{
				#if DEBUG_LEVEL > 1
					Serial.println("Medium button press, no action taken");
				#endif
				blueLEDspacing=0;
				whiteLEDspacing=0;
				bluLEDval=0;
				whtLEDval=0;
				setLEDs();
			}

			buttonpressed=false;
			buttonstage=0;
		}

		if (buttonpressed)
		{
			if ((buttondelta > RESET_BUTTON_WARNING) && (buttonstage == 1))
			{
				buttonstage=2;
				#if DEBUG_LEVEL > 1
					Serial.println("Medium press = button stage 2");
				#endif
				whiteLEDspacing=RESET_BLINK_SPACING;
				blueLEDspacing=RESET_BLINK_SPACING;
				bluLEDval=0;
				whtLEDval=255;
				setLEDs();
			}

			if ((buttondelta > RESET_BUTTON_HOLD) && (buttonstage == 2))
			{
				#if DEBUG_LEVEL > 1
					Serial.println("Long button press, reset to default");
				#endif
				whiteLEDspacing=0;
				blueLEDspacing=0;
				// set to off
				bluLEDval=0;
				whtLEDval=0;
				setLEDs();
				reseteeprom(EEPROM.read(0x09)+0x100*EEPROM.read(0x08)+0x10000*EEPROM.read(0x07)+0x1000000*EEPROM.read(0x06));
				dumpeeprom();
				refreshid(); // is this a good idea? should it reboot instead?
				// this prevents repeating
				buttonstage=4;
			}

			if ((buttondelta > SETUP_BUTTON_HOLD) && (buttonstage == 0))
			{
				buttonstage=1;
				#if DEBUG_LEVEL > 1
					Serial.println("Short press = button stage 1");
				#endif
				if (blueLEDspacing==SETUP_BLINK_SPACING)
				{
					blueLEDspacing=0;
					// visual cue for setup mode off
					bluLEDval=0;
					setLEDs();
				}
				else {blueLEDspacing=SETUP_BLINK_SPACING;}
			}
		}

		oldbutton=button;

	#endif

  if ((setupmode) && ((millis()-setupmodestarted) > SETUP_MODE_TIMEOUT))
  {
    Serial.println("Setup mode timeout");
    setupmode=false;
    blueLEDspacing=0;
    networkid=EEPROM.read(0); // actual network ID  
    deviceid=EEPROM.read(1); // actual device ID
		bluLEDval=0;
		setLEDs();
  }
  
  if (blueLEDspacing)
  {
    if ((millis()-lastblueblink) > blueLEDspacing)
    {
			bluLEDval=255*(!bluLEDval);
			setLEDs();
      lastblueblink=millis();
    }
  }
 
  if (whiteLEDspacing)
  {
    if ((millis()-lastredblink) > whiteLEDspacing)
    {
			whtLEDval=255*(!whtLEDval);
			setLEDs();
      lastredblink=millis();
    }
  }

  if (error)
  {
    if ((millis()-lastredblink) > error)
    {
			redLEDval=255*(!redLEDval);
			setLEDs();
      lastredblink=millis();
    }
  }

  if ((writeenable) && ((millis()-writeenablestarted) > SETUP_MODE_TIMEOUT))
  {
    Serial.println("Write enable timeout");
		Serial.println("****** Write enable false");
    writeenable=false;
  }


	if (!mqttclient.connected())
  {
    uint32_t now = millis();
    if (now - lastReconnectAttempt > 5000)
    {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect())
      {
        lastReconnectAttempt = 0;
      }
    }
  }
  else
  {
    // Client connected
    mqttclient.loop();
  }

	// if so directed by handleReboot, restart
	// this is outside of handleReboot so that the browser renders the "please wait" page patiently
	// with this code in handleReboot, the browser gets stuck waiting for the server to send completely

	if (restartflag)
	{
		#ifdef DISABLE_140T_OUTPUT_FOR_UPDATE
			Serial.println("Turning off for reboot");
			device_specific_setoutput(0);
		#endif
		#ifdef DISABLE_2240T_OUTPUT_FOR_UPDATE
			Serial.println("Turning off for update");
			device_specific_setoutput(0, 0);
    #endif
		ESP.restart();
	}

	// if so directed by handleUpdate, flash the firmware to a new image
	// this is outside of handleUpdate so that the browser renders the "please wait" page patiently
	// with this code in handleUpdate, the browser gets stuck waiting for the server to send completely

  if (flashfirmware)
  {
		Serial.println("Doing the upload");

		// for e.g. devices with triac, turn off output, so as to avoid strobing during update
		#ifdef DISABLE_2240T_OUTPUT_FOR_UPDATE
			Serial.println("Turning off for update");
			device_specific_setoutput(0, 0);
    #endif
		#ifdef DISABLE_140T_OUTPUT_FOR_UPDATE
			Serial.println("Turning off for update");
			device_specific_setoutput(0);
    #endif

    WiFiClient client;

		// bump up watchdog timer so as not to reboot in the middle of updating
		esp_task_wdt_config_t wdtconfig = {
			.timeout_ms = 2 * 60 * 1000, // two minutes
      .idle_core_mask = 0,    // Bitmask of cores
      .trigger_panic = true,			
		};

		esp_task_wdt_reconfigure(&wdtconfig);
	
    httpUpdate.onProgress(update_progress);
	
    httpUpdate.onError(update_error);

    #ifdef WHITE_LED_PIN
      httpUpdate.setLedPin(WHITE_LED_PIN, LOW);
    #endif
  
    t_httpUpdate_return ret = httpUpdate.update(client, firmwarefile);
  
    switch (ret)
    {
      case HTTP_UPDATE_FAILED: Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str()); break;
  
      case HTTP_UPDATE_NO_UPDATES: Serial.println("HTTP_UPDATE_NO_UPDATES"); break;
  
      case HTTP_UPDATE_OK: Serial.println("HTTP_UPDATE_OK"); break;
    }
  }
	
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

uint8_t UPBcommandlength(char UPBcommand[])
{
  return (hexcharstobyte(UPBcommand[0],UPBcommand[1]) & 0b00011111);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void UPBasib(void)
{
	#ifdef ESP_ARDUINO_VERSION_MAJOR 
		Serial.printf("ESP-Arduino              v%d.%d.%d\n",ESP_ARDUINO_VERSION_MAJOR,ESP_ARDUINO_VERSION_MINOR,ESP_ARDUINO_VERSION_PATCH);
	#endif

  #ifdef ESP32US140Tversion
    Serial.printf("ESP32US140T version      %d\n",ESP32US140Tversion);
  #endif

  #ifdef ESP32US2240Tversion
    Serial.printf("ESP32US2240T version     %d\n",ESP32US2240Tversion);
  #endif

  #ifdef ESP32UPBcoreversion
    Serial.printf("ESP32UPBcore version     %d\n",ESP32UPBcoreversion);
  #endif

  #ifdef ESP32UPBeepromversion
    Serial.printf("ESP32UPBeeprom version   %d\n",ESP32UPBeepromversion);
  #endif

  #ifdef ESP32UPBdisplayversion
    Serial.printf("ESP32UPBdisplay version  %d\n",ESP32UPBdisplayversion);
  #endif

  #ifdef ESP32UPBerrorsversion
    Serial.printf("ESP32UPBerrors version   %d\n",ESP32UPBerrorsversion);
  #endif
		
  Serial.println();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void refreshid()
{
	localip={192,168,239,deviceid};
	if (!WiFi.config(localip, gateway, netmask))
	{
		Serial.printf("WiFi failed to configure for 192.168.239.%d\n",deviceid);
		bluLEDval=0;
		setLEDs();
		seterror(100,100);
		return;
	}
	
  char tempstring[100];
  
  for (uint8_t i=0; i<16; i++)
  {
    netv[i]=EEPROM.read(16+i);
    room[i]=EEPROM.read(32+i);
    devn[i]=EEPROM.read(48+i);
  }

  #if DEBUG_LEVEL > 1
    // answering to...
		if (deviceid != EEPROM.read(1))
		{
			Serial.println("----------------------------------------------------");
			Serial.println("Change ID");
			Serial.println("----------------------------------------------------");
		}
    Serial.printf("Network ID:  %02X\n",networkid);
    Serial.printf("UPB ID:      %02X (%d)\n",deviceid,deviceid);
    Serial.printf("Serial:      %u\n",16777216*EEPROM.read(12)+65536*EEPROM.read(13)+256*EEPROM.read(14)+EEPROM.read(15));
    Serial.printf("Password:    %02X%02X\n",EEPROM.read(2),EEPROM.read(3));
    Serial.printf("Network:     %s\n",netv);
    Serial.printf("Room:        %s\n",room);
    Serial.printf("Device name: %s\n",devn);
  #endif
	
	// give the MQTT loop a bit to spin up; otherwise, publish(es) fail
	waitformqttconnect(5000);
	
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-


uint16_t registerchecksum(uint8_t numreg)
{
  // cheating as uint16_t won't ever go over FFFF. we hope.

  uint16_t total=0;
  
  for (uint8_t i=0; i<numreg; i++)
  {
    total=total+EEPROM.read(i);
  }
 
  return total;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void displayUPB(char dir[], char UPBstring[])
{
  char tempstring[100];
    
  // lower five bits of CTL1 are packet length
  uint8_t packetlength=UPBcommandlength(UPBstring);

  Serial.print("   CT1 CT2 NID DST SRC MSG ");
  for (uint8_t i=6; i<packetlength-1; i++)
  {
    Serial.printf("MD%02d ",i-6);
  }

  Serial.println("CS");

  Serial.print(dir);

  for (uint8_t i=0; i<packetlength; i++)
  {
    if (isprintable(UPBstring[2*i])) {Serial.print(UPBstring[2*i]);} else {Serial.printf("[%02X]",UPBstring[2*i]);}		
    if (isprintable(UPBstring[2*i+1])) {Serial.print(UPBstring[2*i+1]);} else {Serial.printf("[%02X]",UPBstring[2*i+1]);}
    
    Serial.print("  ");
    if (i>5) {Serial.print(" ");}
  }

  Serial.println();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// binary data --> chars to send

void sendbinstring(uint8_t binarydata[])
{
	// lower five bits of CTL1 are packet length
	uint8_t packetlength=binarydata[0] & 0b00011111;

	char tempstring[100];
	
	for (uint8_t i=0; i<(packetlength-1); i++)
	{
		int8_t a=binarydata[i] >> 4; if (a>9) {a+=7;} a+=48;
		int8_t b=binarydata[i] & 0xF; if (b>9) {b+=7;} b+=48;
		tempstring[i*2]=a;
		tempstring[i*2+1]=b;
	}
	tempstring[2*(packetlength-1)]=0x00;

	UPBsend(tempstring);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// send *ASCII* command string to UPB (WUPB packet)
// calculates checksum (performed on *binary* versions of the values, ugh)

void UPBsend(char txstring[])
{
	// make a copy
  // manipulating the one passed in is bad string juju
	// this is not always called with space for checksum included
	// typical poor programming on the part of the author
	// as a workaround, create a string of the correct size
	// then set transmit sequence *and* checksum for each send

  uint8_t packetlength=UPBcommandlength(txstring);

	char localstring[100];
	memcpy(localstring,txstring,2*packetlength+1);

	uint8_t ct1=hexcharstobyte(localstring[0],localstring[1]);
	
	uint8_t ct2=hexcharstobyte(localstring[2],localstring[3]);

	// set high bit of CT2 to indicate WUPB
	// against spec, see UPB Description v1.4 actual page 18
	// see above as to why this is done
	ct2=ct2 | 0b10000000;


	// unless ID pulse specified, force to ack pulse. this placates UPStart, but makes all devices appear as Gen1 (less 0x80 packets)
	// may or may not matter
	
	if ((ct2 & 0b00100000) != 0b00100000)
	{
		ct2=(ct2 & 0b10001111) | 0b00010000;
	}

	localstring[2]=bytetohexchar(ct2,HIGH);
	localstring[3]=bytetohexchar(ct2,LOW);
	
	// recalculate checksum
	uint8_t checksum=UPBchecksum(localstring);

	// write checksum into string
	localstring[packetlength*2-2]=bytetohexchar(checksum,HIGH);
	localstring[packetlength*2-1]=bytetohexchar(checksum,LOW);
	
	#if DEBUG_LEVEL > 1
		Serial.printf("Sending %d characters: ",2*packetlength);
		printableprint(localstring,2*(packetlength),true);
	#endif
	
	// display data
	#if DEBUG_LEVEL > 1
		char dispstring[100];
		strcpy(dispstring,localstring);
		dispstring[packetlength*2]=0x00;
		displayUPB("Tx ",dispstring);
	#endif
 
	char sendstring[100];

	memcpy(sendstring,localstring,(packetlength-1)*2);

	sendstring[(packetlength-1)*2]=bytetohexchar(checksum,HIGH);
	sendstring[(packetlength-1)*2+1]=bytetohexchar(checksum,LOW);
	sendstring[(packetlength-1)*2+2]=0x00;
		
	
	if (mqttclient.publish(wupbtopic, sendstring)==false)
	{
		Serial.println("MQTT publish failure");
		seterror(150,104);
	}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void setLEDs()
{
	#if DEBUG_LEVEL > 2
		Serial.printf("Setting individual LEDs to R%d G%d B%d W%d\n",redLEDval,grnLEDval,bluLEDval,whtLEDval);
	#endif

	#ifdef RED_LED_PIN
		digitalWrite(RED_LED_PIN, redLEDval);
	#endif
	
	#ifdef WHITE_LED_PIN
		digitalWrite(WHITE_LED_PIN, whtLEDval);
	#endif
	
	#ifdef BLUE_LED_PIN
		digitalWrite(BLUE_LED_PIN, bluLEDval);
	#endif
	
	#ifdef GREEN_LED_PIN
		ledcWrite(GREEN_LED_PIN, grnLEDval);
//			Serial.printf("Writing %d to ledc channel %d\n",grnLEDval,GREEN_LED_CHANNEL);
	#endif

	#ifdef SECOND_LED_PIN
		ledcWrite(SECOND_LED_PIN, secLEDval);
//			Serial.printf("Writing %d to ledc channel %d\n",secLEDval,SECOND_LED_CHANNEL);
	#endif
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void handleRoot(AsyncWebServerRequest *request)
{
//  Serial.print("Root GET from ");
//  Serial.println(request->client()->remoteIP());

	String devicevid;
	String devicepid;
	
	devicevid=String(bytetohexchar(EEPROM.read(6),HIGH))+
	String(bytetohexchar(EEPROM.read(6),LOW))+
	String(bytetohexchar(EEPROM.read(7),HIGH))+
	String(bytetohexchar(EEPROM.read(7),LOW));
	
	devicepid=String(bytetohexchar(EEPROM.read(8),HIGH))+
	String(bytetohexchar(EEPROM.read(8),LOW))+
	String(bytetohexchar(EEPROM.read(9),HIGH))+
	String(bytetohexchar(EEPROM.read(9),LOW));
  
  String reply;

	// no trailing + when line is just string lit
	
  reply="<HTML><head><title>"+UPBdevicename()+
	"</title></head><body bgcolor=\"#ffffff\" text=\"#000000\" link=\"#0000ff\" alink=\"#0000ff\" vlink=\"#0000ff\">\n"
	+noHTMLlink()+	
	"<h2>"+UPBdevicename()+"</h2><p>\n"+
	"<table>\n"
	"<tr><td>Firmware</td><td>"+String(firmware)+"</td></tr>\n"+
  "<tr><td>written</td><td>"+String(firmwaredate)+" "+String(firmwaretime)+"</td></tr>\n"+
  "<tr><td>Board type</td><td>"+String(ARDUINO_BOARD)+"</td></tr>\n"+
  "<tr><td>Device VID</td><td>"+devicevid+"</td></tr>\n"+
  "<tr><td>Device PID</td><td>"+devicepid+"</td></tr>\n"+
  "<tr><td>MAC address</td><td>"+WiFi.macAddress()+"</td></tr>\n"+
  "<tr><td>WiFi channel</td><td>"+WiFi.channel()+"</td></tr>\n"+
  "<tr><td>WiFi RSSI</td><td>"+WiFi.RSSI()+"dB &#x1F4F6;</td></tr>\n"
  #ifdef ESP_ARDUINO_VERSION_MAJOR 
    "<tr><td>ESP-Arduino</td><td>"+
    String(ESP_ARDUINO_VERSION_MAJOR)+"."+
    String(ESP_ARDUINO_VERSION_MINOR)+"."+
    String(ESP_ARDUINO_VERSION_PATCH)+
    "</td><tr>\n"+
  #endif
  #ifdef ESP32US140Tversion
    "<tr><td>ESP32US140Tversion</td><td>"+String(ESP32US140Tversion)+"</td><tr>\n"+
  #endif
  #ifdef ESP32US2240Tversion
    "<tr><td>ESP32US2240Tversion</td><td>"+String(ESP32US2240Tversion)+"</td><tr>\n"+
  #endif
  #ifdef ESP32UPBcoreversion
    "<tr><td>ESP32UPBcoreversion</td><td>"+String(ESP32UPBcoreversion)+"</td><tr>\n"+
  #endif
  #ifdef ESP32UPBeepromversion
    "<tr><td>ESP32UPBeepromversion</td><td>"+String(ESP32UPBeepromversion)+"</td><tr>\n"+
  #endif
  #ifdef ESP32UPBdisplayversion
    "<tr><td>ESP32UPBdisplayversion</td><td>"+String(ESP32UPBdisplayversion)+"</td><tr>\n"+
  #endif
  #ifdef ESP32UPBerrorsversion
    "<tr><td>ESP32UPBerrorsversion</td><td>"+String(ESP32UPBerrorsversion)+"</td><tr>\n"+
  #endif
	"</table><p>\n"+
	
	device_specific_root();

  uint8_t errornum=EEPROM.read(0x1CE);
  if (errornum)
  {
    uint16_t parm1=EEPROM.read(0x1D0)*0x100+EEPROM.read(0x1D1);
    uint16_t parm2=EEPROM.read(0x1D2)*0x100+EEPROM.read(0x1D3);
    reply=reply+"<font color=\"#800000\">Error "+String(errornum)+" Param1 "+String(parm1)+" Param2 "+String(parm2)+"</font><p>\n";
  }

	reply=reply+"Setup mode <b>";
	if (setupmode)
	{
		reply=reply+"Yes";
	}
	else
	{
		reply=reply+"No";
	}
	reply=reply+"</b><p>";

	reply=reply+
	"<a href=\"/entersetupmode?return=/\">&#x260B; Enter setup mode</a> &nbsp;\n"
	"<a href=\"/exitsetupmode?return=/\">&#x260A; Exit setup mode</a> &nbsp;\n"
  "<a href=\"/upbcommand?cmd=30&return=/\">&#x260E; Report status</a> &nbsp;\n"
  "<a href=\"/upbcommand?cmd=0C&return=/\">&#x2705; Clear error</a> &nbsp;\n"
	"<a href=\"/edit\">&#x270E; Edit settings</a> \n"
	"<a href=\"/update\">&#x1F5F2; Upload firmware</a> &nbsp; \n"
	"<a href=\"/reboot\">&#x21f5; Reboot</a> &nbsp; \n"
	"<a href=\"/\">&#x21bb; Refresh</a><br>\n"
	
  "</body></html>";

  request->send(200, "text/html", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void handleUpdate(AsyncWebServerRequest *request)
{
  String filename;
  filename="";
  bool dotheupload=false;

	String boardtype=String(ARDUINO_BOARD);

	String devicetype=firmware;
	devicetype=devicetype.substring(1+devicetype.lastIndexOf("\\"));
	devicetype=devicetype.substring(0,devicetype.indexOf("_"));
	
  // odd order of <html> and <body> tags allows easy insertion of
  // <meta refresh...> only when upload actually occurs

  String reply;
  reply="<body>"+noHTMLlink();

  if (request->hasParam("filename"))
  {
      filename= request->getParam("filename")->value();
//      Serial.print("Filename ");
//      Serial.println(filename);
  }

  if (filename.equals(""))
  {
		String serverPath;
		
		HTTPClient http;
		serverPath="http://192.168.123.14/cgi-bin/wupbfilenames.pl?boardtype="+boardtype+"&devicetype="+devicetype;
		http.begin(serverPath.c_str());
						
		// Send HTTP GET request
		int httpResponseCode = http.GET();
		
		if (httpResponseCode>0)
		{
			#if DEBUG_LEVEL > 1
				Serial.print("HTTP Response code: ");
				Serial.println(httpResponseCode);
			#endif
			reply = http.getString();
		}
		else
		{
			#if DEBUG_LEVEL > 1
				Serial.print("Error code: ");
				Serial.println(httpResponseCode);
			#endif
			reply="Error "+String(httpResponseCode);
		}
		// Free resources
		http.end();
		request->send(200, "text/html", reply);
		return;
  }
  else
  {
		#if DEBUG_LEVEL > 1
			Serial.print("Directed to update to ");
			Serial.println(filename);
		#endif

    String boardtype=String(ARDUINO_BOARD);
    boardtype.toLowerCase();
    int8_t boardtypefound=filename.indexOf(boardtype);
  
    if (boardtypefound==-1)
    {
			Serial.println("Board mismatch");
			Serial.print("This board is ");
			Serial.print(boardtype);
			Serial.print(" but filename is ");
			Serial.println(filename);
      reply=reply+"Board mismatch; this board is </b>"+boardtype+"</b> but filename is <b>"+filename+"</b>";
    }
    else
    {
			Serial.print("Update filename ");
			Serial.println(filename);

      reply="<meta http-equiv=\"refresh\" content=\"40; URL=/\">"+reply+"Uploading file <b>"+filename+"</b>. This page will refresh to the device home, be patient.<p>";
      dotheupload=true;
    }
  }  
	
	reply="<html>"+reply+"</body></html>";
	request->send(200, "text/html", reply);

	if (dotheupload)
	{
		flashfirmware=true;
		firmwarefile="http://192.168.123.14/wupb/"+filename;
	}

	return;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void handleReboot(AsyncWebServerRequest *request)
{
	String reply;
  reply="<meta http-equiv=\"refresh\" content=\"10; URL=/\">Rebooting";
  request->send(200, "text/html", reply);
	restartflag=true;
//	delay(500);
//	ESP.restart();
}

// process any valid commands or mark as invalid

void processUPB(char UPBstring[])
{
	// ignore 0x80-0xFF reports from other devices
	uint8_t msgid=hexcharstobyte(UPBstring[0x0A],UPBstring[0x0B]);

	if (msgid>=0x80)
	{
		#if DEBUG_LEVEL > 2
			Serial.printf("Ignoring MDID %02X report\n",msgid);
		#endif
		return;
	}
 
  validcommand=false;
  addressed=false;
  uint8_t packetlength=UPBcommandlength(UPBstring);
	
  #if DEBUG_LEVEL > 1
    Serial.printf("Received UPB data of %d bytes\n",packetlength);
  #endif

  // main UPB command processing

  uint8_t UPBdata[packetlength];

  for (uint8_t i=0; i<packetlength; i++)
  {
    UPBdata[i]=hexcharstobyte(UPBstring[2*i],UPBstring[2*i+1]);
  }

	// -*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
	// include device-specific command values and code
	// to allow this file to remain generic and unmodified per-device
	// only updated for new features or bugfixes
	// processing *outside* the addressed check above allows this code
	// to check for commands or links sent to other devices
	// returns true to bypass regular processing 
	// -*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-
	
	if (device_specific_command(UPBdata)==false)
	{
		uint8_t ct1=UPBdata[0];
		uint8_t ct2=UPBdata[1];
		uint8_t nid=UPBdata[2];
		uint8_t dst=UPBdata[3];
		uint8_t src=UPBdata[4];
		uint8_t msg=UPBdata[5];

		bool linkflag=UPBdata[0]>>7;
		uint8_t linknum;
		uint8_t regstart;
		uint8_t regcount;
		
		uint8_t linklevel=100;

		addressed=false;

		// device addressing -- just compare to ID. don't use deviceid as it is 254 during setup mode
		if ((dst == EEPROM.read(1)) && (linkflag == 0))
		{
			#if DEBUG_LEVEL > 1
				Serial.printf("Addressed as device %d\n",dst);
			#endif
			addressed=true;
		}

		if ((dst == 253) && (writeenable) && (linkflag == 0))
		{
			#if DEBUG_LEVEL > 1
				Serial.println("Addressed as write-enabled device");
			#endif
			addressed=true;
		}

		if ((dst == 254) && (setupmode) && (linkflag == 0))
		{
			#if DEBUG_LEVEL > 1
				Serial.println("Addressed as setup mode device");
			#endif
			addressed=true;
		}

		// destination 0 is broadcast, all devices listen
		if ((dst == 0) && (linkflag == 0))
		{
			#if DEBUG_LEVEL > 1
				Serial.println("Addressed via broadcast");
			#endif
			addressed=true;
		}

		// link addressing -- must compare to table
		
		if (linkflag==1)
		{
			device_generic_command(UPBdata);
		}

		// show incoming just for us unless higher debug

		if (addressed)
		{
			#if DEBUG_LEVEL > 1
				displayUPB("Rx ",UPBstring);
			#endif
		}
		else
		{
			#if DEBUG_LEVEL > 2
				displayUPB("Rx ",UPBstring);
			#endif
		}

		if (addressed) // That's us
		{
			uint8_t ackreq=(ct2 & 0b01110000) >> 4;

			// ID pulses on broadcast works on the line, each device pops its own pulse and no others
			// this is for UPBv2 enumeration, see UPB Description 1.4 actual pages 22-23
			// but doesn't work with WUPB, as the bridge would have to wait for all the packets and then generate pulses
			// and the bridge has no idea how many devices there are
			if ((dst==0) && (ackreq == 0b010))
			{
				Serial.println("Broadcast ID pulse requested, changing to ack pulse");
				ackreq=0b001;
				ct2=(ct2 & 0b10001111) | 0b00010000;
			}

			// if any type of ack requested, send an 0x80 with same CT2 as incoming packet
			// bridge converts to pulses as needed
			// send first thing so as to placate UPStart
			
			if (ackreq)
			{
				sprintf(ackpacket,"08%02X02%02X%02X80%02XCS\x00",ct2,src,thisIP,msg);
				#if DEBUG_LEVEL > 1
					Serial.printf("Ack packet %s\n",ackpacket);
				#endif
				UPBsend(ackpacket);
			}
			
			uint8_t optbytes=packetlength-7;
		
			// these commands should be valid for all device types
		
			// ping, no options
			if ((msg == 0x00) && (optbytes==0))
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Ping");
				#endif
				return;
			}

			// null command, any options
			if ((msg == 0x00) && (optbytes))
			{
				validcommand=true;
//				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Null");
				#endif
				return;
			}

			// write enable, requires password
			if ((msg == 0x01) && (optbytes==2) && (UPBdata[6]==EEPROM.read(2)) && (UPBdata[7]==EEPROM.read(3)))
			{
				validcommand=true;
				clearstorederror();
				Serial.println("Write enable true");
				writeenable=true;
				writeenablestarted=millis();

				return;
			}

			// write protect
			if (msg == 0x02)
			{
				validcommand=true;
				clearstorederror();
				Serial.println("Write enable false");
				writeenable=false;
				setupmode=false;
				bluLEDval=0;
				setLEDs();

				seteepromchecksum();
				EEPROM.commit();
				return;
			}

			// enter setup mode, requires password
			if ((msg == 0x03) && (optbytes==2) && (UPBdata[6]==EEPROM.read(2)) && (UPBdata[7]==EEPROM.read(3)))
			{
				validcommand=true;
				clearstorederror();
				Serial.println("Enter setup mode");
				setupmode=true;
				blueLEDspacing=SETUP_BLINK_SPACING;
				setupmodestarted=millis();
				networkid=0x00; // broadcast network ID
				deviceid=0xFE; // broadcast device ID 
				return;
			}

			// exit setup mode
			if (msg == 0x04)
			{
				validcommand=true;
				clearstorederror();
				Serial.println("Exit setup mode");
				bluLEDval=0;
				setLEDs();

				setupmode=false;
				blueLEDspacing=0;

				networkid=EEPROM.read(0); // actual network ID  
				deviceid=EEPROM.read(1); // actual device ID
				
				// only if the device or network ID registers were changed
				// should the device be restarted with the new address
				if (refreshidflag)
				{
					refreshidflag=false;
					Serial.println("Rebooting from ID change");
					ESP.restart();
				}
				
				return;
			}

			// get setup time
			if (msg == 0x05)
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Get setup time");
				#endif
				
				bindata[0]=0x0A;  		  // CTL1
				bindata[1]=EEPROM.read(0x8E) & 0b00001100;  	// CTL2 includes transmission count
				bindata[2]=networkid;   // NID
				bindata[3]=src;        	// dest (requester)
				bindata[4]=deviceid;    // src (me)
				bindata[5]=0x85;        // setup time report
				bindata[6]=0x42;				// random
				bindata[7]=(millis()-setupmodestarted)/2; // should actually be /2.133, whatever
				sendbinstring(bindata);
				
				return;
			}

			// get current error
			// works the same as 0x10 CC 0C (read CC-D7) except without clearing error
			if (msg == 0x07) 
			{
				validcommand=true;
				bindata[0]=15;			  	// CTL1
				bindata[1]=EEPROM.read(0x8E) & 0b00001100;  	// CTL2 includes transmission count
				bindata[2]=networkid;   // NID
				bindata[3]=src;        	// dest (requester)
				bindata[4]=deviceid;    // src (me)
				bindata[5]=0x87;        // error report
				for (uint8_t i=0; i<0x08; i++) {bindata[6+i]=EEPROM.read(0x1CC+i);}

				#if DEBUG_LEVEL > 1
					Serial.println("Get error data");
					Serial.printf("Error code   %02X\n",bindata[0x09]);
					Serial.printf("Error length %04X\n",bindata[0x07]*0x100+bindata[0x06]);
					Serial.printf("Error parm 1 %04X\n",bindata[0x0A]*0x100+bindata[0x0B]);
					Serial.printf("Error parm 2 %04X\n",bindata[0x0C]*0x100+bindata[0x0D]);
				#endif

				sendbinstring(bindata);
				return;
			}

			// set current error
			// works the same as 0x11 CC 02 and 0x10 CE 01 (write CC-CE), except without clearing error
			if (msg == 0x08) 
			{
				validcommand=true;
				error=UPBdata[8]*0x100+UPBdata[7];
				EEPROM.write(0x1CE,UPBdata[6]);
				EEPROM.write(0x1CD,UPBdata[7]);
				EEPROM.write(0x1CC,UPBdata[8]);
				commitflag=true;

				#if DEBUG_LEVEL > 1
					Serial.printf("Set error code %d length %d\n",UPBdata[6],UPBdata[8]*0x100+UPBdata[7]);
				#endif

				if (error==0)
				{
					redLEDval=0;
					setLEDs();
				}
				return;
			}

			// clear  error
			if (msg == 0x0C) 
			{
				clearstorederror();
				
				validcommand=true;

				#if DEBUG_LEVEL > 1
					Serial.println("Clear error\n");
				#endif

				redLEDval=0;
				setLEDs();
				return;
			}


			// transmit this message
			if (msg == 0x0d)
			{
				validcommand=true;
				clearstorederror();
				
				Serial.print("Transmitting this message: ");
				
				for (uint8_t i=0; i<optbytes; i++)
				{
					bindata[i]=UPBdata[6+i];
					Serial.printf("%02X ",bindata[i]);
				}
				Serial.println();

				sendbinstring(bindata);
				return;
			}


			// reset device
			if ((msg == 0x0E) && (optbytes==2) && (UPBdata[6]==EEPROM.read(2)) && (UPBdata[7]==EEPROM.read(3)))
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Restart");
				#endif
				delay(1000);
				ESP.restart();
				return;
			}

			// get device signature
			if (msg == 0x0f) 
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Get signature");
				#endif

				bindata[0]=0x18;      // CTL1
				bindata[1]=EEPROM.read(0x8E) & 0b00001100;  // CTL2 includes transmission count
				bindata[2]=networkid; // NID
				bindata[3]=src;       // dest (requester)
				bindata[4]=deviceid;  // Src (me)
				bindata[5]=0x8F;      // Device signature report

				uint8_t CS1H=registerchecksum(64) >> 8;
				uint8_t CS1L=registerchecksum(64) & 0xFF;
				
				uint8_t CS2H=registerchecksum(192)>> 8;
				uint8_t CS2L=registerchecksum(192) & 0xFF;
				
				bindata[6]=0x2A;      // random number high
				bindata[7]=0xAF;      // random number low
				bindata[8]=0xFF;			// signal strength is meaningless for WUPB
				bindata[9]=0x00;			// noise floor is meaningless for WUPB
				bindata[10]=CS1H;
				bindata[11]=CS1L;
				bindata[12]=CS2H;
				bindata[13]=CS2L;

				// number of registers, varies by device
				#ifdef ESP32US140Tversion				
					bindata[14]=0xC0;
				#endif
				#ifdef ESP32US2240Tversion				
					bindata[14]=0xF6;
				#endif
				
				bindata[15]=0x00;      // diagnostic 0
				bindata[16]=0x00;      // diagnostic 1
				bindata[17]=0x00;      // diagnostic 2
				bindata[18]=0x00;      // diagnostic 3
				bindata[19]=0x00;      // diagnostic 4
				bindata[20]=0x00;      // diagnostic 5
				bindata[21]=0x00;      // diagnostic 6
				bindata[22]=0x00;      // diagnostic 7
				
				sendbinstring(bindata);
				return;
			}

			// get registers
			if (msg == 0x10) 
			{
				validcommand=true;
				
				regstart=UPBdata[6];
				regcount=UPBdata[7];

				#if DEBUG_LEVEL > 1
					Serial.printf("Get register(s) %02X-%02X = ",regstart,regstart+regcount-1);
					Serial.println(); // remove
				#endif

				bindata[0]=8+regcount;  // CTL1
				bindata[1]=EEPROM.read(0x8E) & 0b00001100;  	// CTL2 includes transmission count
				bindata[2]=networkid;   // NID
				bindata[3]=src;        	// dest (requester)
				bindata[4]=deviceid;    // src (me)
				bindata[5]=0x90;        // register report
				bindata[6]=regstart;    // number

				for (uint8_t i=0; i<regcount; i++)
				{
					bindata[7+i]=EEPROM.read(regstart+i);
					#if DEBUG_LEVEL > 1
						Serial.printf("%02X ",bindata[7+i]); 
					#endif
				}
				#if DEBUG_LEVEL > 1
					Serial.println();
				#endif

				sendbinstring(bindata);
				return;
			}

			// set registers
			if ((msg == 0x11) && !writeenable && !setupmode)
			{
				Serial.println("Shouldn't set registers unless write enable off or in setup mode");
			}

//			if ((msg == 0x11) && (writeenable || setupmode))
			if (msg == 0x11)
			{
				validcommand=true;
				clearstorederror();
				regstart=UPBdata[6];
				regcount=packetlength-8;

				if (regcount > 0x10)
				{
					Serial.println("Excessive register count to write");
					regcount=0x10;
				}
				
				#if DEBUG_LEVEL > 1
					Serial.printf("Set register(s) %02X-%02X = ",regstart,regstart+regcount-1);
				#endif

				for (uint8_t i=0; i<regcount; i++)
				{
					#if DEBUG_LEVEL > 1
						Serial.printf("%02X ",UPBdata[7+i]);
					#endif
					
					EEPROM.write(regstart+i,UPBdata[7+i]);

					// if the UPB ID is rewritten, redisplay ID info
					if ((regstart+i) == 0)
					{
						if (networkid != UPBdata[7+i])
						{
							Serial.println("Changed network ID");
							networkid=UPBdata[7+i];
							refreshidflag=true;
						}
					}
					if ((regstart+i) == 1)
					{
						if (deviceid != UPBdata[7+i])
						{
							Serial.println("Changed device ID");
							deviceid=UPBdata[7+i]; 
							refreshidflag=true;
						}
					}

					#ifdef ESP32US140Tversion
						// if the fade option is changed, update accordingly 
//						if ((regstart+i) == 0x8D) {globalfaderate=EEPROM.read(0x8D) & 0x0F;} // TBD
					#endif
						
					#ifdef ESP32US2240Tversion
						// if either fade option is changed, update accordingly 
//						if ((regstart+i) == 0x88) {globalfaderate1=EEPROM.read(0x88) & 0x0F;} // TBD
//						if ((regstart+i) == 0x89) {globalfaderate2=EEPROM.read(0x89) & 0x0F;} // TBD
					#endif
						
				}
				
				#if DEBUG_LEVEL > 1
					Serial.println();
				#endif
	
				seteepromchecksum();
				EEPROM.commit(); // immediately write in this case, so reads turn around properly
				commitflag=false;
				return;
			}    

			// dump EEPROM to serial monitor
			if ((msg == 0x12) && (optbytes==0))
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Dump EEPROM");
				#endif
				dumpeeprom();
				return;
			}

			// heartbeat report
			if (msg == 0x13)
			{
				validcommand=true;
				clearstorederror();

				heartbeat(src);
				return;
			}

			// diagnostic error mode
			if (msg == 0x14)
			{
				validcommand=true;
				seterror(250, 105);
				return;
			}

			// reset (to defaults, ie reset EEPROM), requires password
			if ((msg == 0x1F) && (optbytes==2) && (UPBdata[6]==EEPROM.read(2)) && (UPBdata[7]==EEPROM.read(3)))
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Reset EEPROM");
				#endif
				reseteeprom(EEPROM.read(0x09)+0x100*EEPROM.read(0x08)+0x10000*EEPROM.read(0x07)+0x1000000*EEPROM.read(0x06));
				refreshid();
				return;
			}

			// get status report
			if (msg == 0x30) 
			{
				validcommand=true;
				clearstorederror();
				#if DEBUG_LEVEL > 1
					Serial.println("Sending status report");
				#endif

				reportstatus(src);

				return;
			}

			if (linkflag==0) {device_generic_command(UPBdata);}
		}
	}
	
	if ((addressed) && (!validcommand))
	{
		seterror(250, 102);
		// hack to allow further movement on shade even after unknown commands
		error=0;
		
		// save packet into 0x100+
		for (uint8_t i=0; i<packetlength; i++)
		{
			EEPROM.write(0x100+i,UPBdata[i]);
		}
		EEPROM.commit();

		Serial.printf("Packet length %d\n",packetlength);

		Serial.println("Invalid command");
		Serial.print("CT1 CT2 NID DST SRC MSG ");
		for (uint8_t i=6; i<packetlength-1; i++)
		{
			Serial.printf("MD%02d ",i-6);
		}

		Serial.println("CS");

		for (uint8_t i=0; i<packetlength; i++)
		{
			Serial.printf("%02X  ",UPBdata[i]);
			if (i>13) {Serial.print("  ");}
		}
		Serial.println("\n");
		return;
	}
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void heartbeat(uint8_t who)
{
	#if DEBUG_LEVEL > 0
		Serial.println("Heartbeat");
	#endif
	bindata[0]=0x07;
	bindata[1]=EEPROM.read(0x8E) & 0b00001100;  // CTL2 includes transmission count
	bindata[2]=networkid; // NID
	bindata[3]=who;       // Dest (requesting source)
	bindata[4]=deviceid;  // Src (me)
	bindata[5]=0x93;      // Heartbeat report
	sendbinstring(bindata);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String UPBdevicename(void)
{
	char roomname[17];
	roomname[16]=0x00;
	char devicename[17];
	devicename[16]=0x00;

	for (uint8_t i=0; i<16; i++) {roomname[i]=EEPROM.read(32+i);}
	for (uint8_t i=0; i<16; i++) {devicename[i]=EEPROM.read(48+i);}
	
//	Serial.print("Room   "); Serial.println(roomname);
//	Serial.print("Device "); Serial.println(devicename);
	
	return String(roomname)+" "+String(devicename);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// save options
// REG8E=xxxx01xx writes bits 3&2 masked to register 0x8E
// REG12=34 writes 0x34 to register 0x12
// RGD56=78 writes *decimal* 78 to register 0x56 (for links)

void handleSaveSettings(AsyncWebServerRequest *request)
{
  uint8_t registerhex;
  uint8_t valuehex;
  uint8_t binaryvalue;
  uint8_t binarymask;
  uint8_t existingvalue;
  
	String returl="/edit";
  String reply="";

  uint8_t paramcount=request->params();

  for (uint8_t i=0; i<paramcount; i++)
  {
    if ((request->argName(i).indexOf("URL")) != -1)
    {
      returl=request->arg(i);
			Serial.print("Return to URL ");
			Serial.println(returl);
		}
	}

  reply="<meta http-equiv=\"refresh\" content=\"0; URL="+returl+"\">Refresh to <a href=\""+returl+"\">"+returl+"</a>";

  for (uint8_t i=0; i<paramcount; i++)
  {
    if ((request->argName(i).indexOf("RGD")) != -1)
    {
      String regstring=request->argName(i).substring(3,5);
      registerhex=hexStringToInt(regstring);

      String value=request->arg(i);
			int valuedec=value.toInt();
			reply=reply+"Write "+String(valuedec)+" to register "+registerhex+"<p>\n";
			EEPROM.write(registerhex,valuedec);
			commitflag=true;
		}

	
    if ((request->argName(i).indexOf("REG")) != -1)
    {
      String regstring=request->argName(i).substring(3,5);
      registerhex=hexStringToInt(regstring);

      String value=request->arg(i);
      if (value.length() == 8)
      {
        String bitmask;
        bitmask=value;
        // this takes a value of eg "xxxx01xx"
        // and changes it into a mask of "11110011"
        // which is AND'd with the existing value
        // then OR'd with the given value to create the final value
        bitmask.replace("1","0");
        bitmask.replace("x","1");
        bitmask.replace("X","1");
        value.replace("x","0");
        value.replace("X","0");
      
        existingvalue=EEPROM.read(registerhex);
        binarymask=binStringToInt(bitmask);
        binaryvalue=binStringToInt(value);
        EEPROM.write(registerhex,existingvalue & binarymask | binaryvalue);
        commitflag=true;
      }
      else
      {
        valuehex=hexStringToInt(value);
        EEPROM.write(registerhex,valuehex);
        commitflag=true;
      }
    }
  }

  request->send(200, "text/html", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// basically special case of handleUPBcommand

void handleEnterSetupMode(AsyncWebServerRequest *request)
{
	Serial.println("Enter setup mode via web UI");
	setupmode=true;
	blueLEDspacing=SETUP_BLINK_SPACING;
	setupmodestarted=millis();
	networkid=0x00; // broadcast network ID
	deviceid=0xFE; // broadcast device ID 
  String reply;

	String returnurl="/edit";

	if (request->hasParam("return"))
	{
		returnurl=request->getParam("return")->value();
	}

  reply="<meta http-equiv=\"refresh\" content=\"0; URL="+returnurl+"\">Refresh to <a href=\""+returnurl+"\">"+returnurl+"</a>";
  request->send(200, "text/html", reply);
	
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// basically special case of handleUPBcommand

void handleExitSetupMode(AsyncWebServerRequest *request)
{
	Serial.println("Exit setup mode via web UI");
	blueLEDspacing=0;
	setupmode=false;
	writeenable=false;
	bluLEDval=0;
	setLEDs();
	networkid=EEPROM.read(0); // actual network ID  
	deviceid=EEPROM.read(1); // actual device ID
  String reply;

	String returnurl="/edit";

	if (request->hasParam("return"))
	{
		returnurl=request->getParam("return")->value();
	}

  reply="<meta http-equiv=\"refresh\" content=\"0; URL="+returnurl+"\">Refresh to <a href=\""+returnurl+"\">"+returnurl+"</a>";
  request->send(200, "text/html", reply);
	
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// parses /upbcommand?cmd=XX[&opt=YY]
// cmd in hex, but opt in dec
// because it just works better, that's why (cmd=22 opt=100 for %age)

void handleUPBcommand(AsyncWebServerRequest *request)
{
  String reply;

	String returnurl="/edit";

	if (request->hasParam("return"))
	{
		returnurl=request->getParam("return")->value();
	}

  reply="<meta http-equiv=\"refresh\" content=\"0; URL="+returnurl+"\">Refresh to <a href=\""+returnurl+"\">"+returnurl+"</a>";

  uint8_t passedcmd;
  String passedopt;

  uint8_t proccmdlen=0;

  uint8_t chkforcmd=request->hasParam("cmd");
  if (chkforcmd==0)
  {
    reply="No command. Fail.";
  }
  else
  {
    proccmdlen=7;

    passedcmd=hexStringToInt(request->getParam("cmd")->value());

    if (request->hasParam("opt"))
    {
      passedopt=request->getParam("opt")->value();
      proccmdlen++;
    }
  }

  request->send(200, "text/html", reply);

  // only send if command was specified
  if (proccmdlen)
  {
    String proccmdstring;
    proccmdstring=makeNiceHexString(proccmdlen);
    proccmdstring=proccmdstring+makeNiceHexString(EEPROM.read(0x8E) & 0b00001100);
    proccmdstring=proccmdstring+makeNiceHexString(networkid);
    proccmdstring=proccmdstring+makeNiceHexString(thisIP); // dest
    proccmdstring=proccmdstring+makeNiceHexString(thisIP); // src
    proccmdstring=proccmdstring+makeNiceHexString(passedcmd);
    if (!passedopt.equals(""))
    {
      proccmdstring=proccmdstring+makeNiceHexString(passedopt.toInt());
    }

    char proccmdarray[proccmdstring.length()+2]={0};
    proccmdstring.toCharArray(proccmdarray,proccmdstring.length()+1);
		
		char csum[3]={0};
		
		sprintf(csum,"%02X",UPBchecksum(proccmdarray));
		
		proccmdarray[proccmdstring.length()]=csum[0]; // never actually used
		proccmdarray[proccmdstring.length()+1]=csum[1]; // never actually used
		proccmdarray[proccmdstring.length()+2]=0x00;
		
    processUPB(proccmdarray);
  }      
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String noHTMLlink(void)
{
	return "<style type=\"text/css\">\n"
	"<!-- A:link {text-decoration:none} A:visited {text-decoration:none} A:active {text-decoration:none}-->\n"
	"</style>\n";
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// /sendthiscommand?cmd=C1C2NIDSSRMD..CS
// cmd in hex, but opt in dec
// because it just works better, that's why (cmd=22 opt=100 for %age)

void handleSendThisCommand(AsyncWebServerRequest *request)
{
  String reply;

  uint8_t chkforcmd=request->hasParam("cmd");
  if (chkforcmd==0)
  {
    reply="No command. Fail.";
  }
  else
  {
		String sendthiscmdstring=request->getParam("cmd")->value();
    char sendthiscmdarray[2*(sendthiscmdstring.length()+1)];
    sendthiscmdstring.toCharArray(sendthiscmdarray,2*(sendthiscmdstring.length()+1));
    processUPB(sendthiscmdarray);
		reply=sendthiscmdstring+" sent.<p>";
  }      		
	
  request->send(200, "text/html", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void handleDumpRegisters(AsyncWebServerRequest *request)
{
	String reply;
	reply="";
	
	char regbyte[3];
	
	for (uint16_t i=0; i<512; i++)
	{
		sprintf(regbyte,"%02X",EEPROM.read(i));
		reply=reply+String(regbyte);
	}
	
	request->send(200, "text/plain", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

bool reconnect()
{
	// use MAC addr as MQTT ID
	// this means that even if a duplicate IP comes online, the MQTT doesn't go completely stupid
	// and still just reboots when things go awry
  	
	String mqttid=WiFi.macAddress();
	mqttid.replace(":","");
	char devname[13];
	mqttid.toCharArray(devname,13);
	Serial.print("MQTT identifier: ");
	Serial.println(devname);
	
	if (mqttclient.connect(devname))
  {
    // Once connected, (re)subscribe
    mqttclient.subscribe(wupbtopic);
  }
  return mqttclient.connected();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void mqttcallback(char* topic, byte* payload, uint16_t msglength)
{
	char msgarray[msglength+1];
  memcpy(msgarray,payload,msglength);

	#if DEBUG_LEVEL > 1
		Serial.printf("[%s] ",wupbtopic);
		printableprint(msgarray,msglength,true);
	#endif
	
	processUPB(msgarray);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void waitformqttconnect(uint32_t cycletime)
{
	uint32_t starttime=millis();
	
	#if DEBUG_LEVEL > 1
		Serial.println("Awaiting MQTT connect");
	#endif
	while ((millis() - starttime) < cycletime)
	{
		if (!mqttclient.connected())
		{
			uint32_t now = millis();
			if (now - lastReconnectAttempt > 5000)
			{
				lastReconnectAttempt = now;
				// Attempt to reconnect
				if (reconnect())
				{
					lastReconnectAttempt = 0;
				}
			}
		}
		else
		{
			// Client connected
			mqttclient.loop();
		}
	}
	#if DEBUG_LEVEL > 1
		Serial.println("MQTT connected");
	#endif
}