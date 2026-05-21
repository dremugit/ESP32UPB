// have to do these before including ESP32US2240T.h

// these for demo on ESP32 WROOM board
#define BLUE_LED_PIN           2          // shared with output
                                          // so setup mode confuses things
#define BUTTON_PIN            18          // setup mode button

// 0...3    0 = serial output is low or off; higher is increasingly verbose
#define DEBUG_LEVEL       1

#include <ESP32US2240T.h>

// device specific hardware

#define ONBOARD_LED_PIN           2          // internal state LED

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void setup(void)
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nAnd so it begins");
  Serial.println(__FILE__);
  Serial.print(__TIME__);
  Serial.print(" ");
  Serial.println(__DATE__);
  
  // this has to be set in this file so as not to read "ESP32UPB.h"
  firmware=__FILE__;
  firmwaretime=__TIME__;
  firmwaredate=__DATE__;
  ESP32US2240Tsetup();

  pinMode(2, OUTPUT);

  setdevicestate(EEPROM.read(0xF6),1);
  setdevicestate(EEPROM.read(0xF7),2);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void loop(void)
{
  ESP32US2240Tloop();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// called in processUPB() at the beginning, so that this can take the
// place of existing commands 
// set validcommand=true, for any extra commands handled here
// (see processUPB for examples)
// return true to skip processing of existing commands if handled
// differently here

// returns false to process all commands as normal
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

bool device_specific_command(uint8_t UPBdata[])
{
  return false; 
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// outputs the specified level, if additional hardware is present
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

// LED is shared between channels

void device_specific_setoutput(uint8_t dimlevel, uint8_t outputchannel)
{
  #if (DEBUG_LEVEL>0)
    Serial.printf("Output level %d\n",dimlevel);
  #endif

  digitalWrite(ONBOARD_LED_PIN, dimlevel/100);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void handleEditSettings(AsyncWebServerRequest *request)
{
  String reply;

  reply="<HTML><head><title>"+UPBdevicename()+
  " edit settings</title></head><body bgcolor=\"#ffffff\" text=\"#000000\" link=\"#0000ff\" alink=\"#0000ff\" vlink=\"#0000ff\">\n"
  +noHTMLlink()+
  "<table cellpadding=3 border=2px>\n"
  "<tr>\n"
  "<td><a href=\"/upbcommand?cmd=14&return=/edit\">&#x274C;Cause error</a></td>\n"
  "<td><a href=\"/upbcommand?cmd=0C&return=/edit\">&#x2705;Clear error</a></td>\n"
  "<td><a href=\"/upbcommand?cmd=22&opt=100&return=/edit\">&#x2191;On</td>\n"
  "<td><a href=\"/upbcommand?cmd=22&opt=00&return=/edit\">&#x2193;Off</td>\n"
  "</tr>\n"

  "</table><p>\n"
  
  "<a href=\"/\">&#x2302; Home</a> &nbsp; <a href=\"/edit\">&#x21bb; Refresh</a><p>\n";
  request->send(200, "text/html", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String device_specific_root(void)
{
  String reply;
  reply="<p>Current channel 1 UPB status is "+String(EEPROM.read(0xF6))+"%<br>\n";
  reply=reply+"Current channel 2 UPB status is "+String(EEPROM.read(0xF7))+"%<p>\n";

  return reply;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void reportstatus(uint8_t who)
{
  bindata[0]=0x09;      // CTL1
  bindata[1]=EEPROM.read(0x8E) & 0b00001100;  // CTL2 includes transmission count
  bindata[2]=networkid; // NID
  bindata[3]=who;        // Dest (requesting source)
  bindata[4]=deviceid;  // Source (me)
  bindata[5]=0x86;      // Device status report
  bindata[6]=EEPROM.read(0xF6);
  bindata[7]=EEPROM.read(0xF7);
  
  sendbinstring(bindata);

}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
