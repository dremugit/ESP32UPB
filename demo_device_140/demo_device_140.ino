// have to do these before including ESP32US140T.h

// these for demo on ESP32 WROOM board
#define BLUE_LED_PIN           2          // shared with output
                                          // so setup mode confuses things
#define BUTTON_PIN            18          // setup mode button

#define GREEN_LED_CHANNEL     0

// 0...3    0 = serial output is low or off; higher is increasingly verbose
#define DEBUG_LEVEL       0

#include <ESP32US140T.h>

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
  ESP32US140Tsetup();

  pinMode(ONBOARD_LED_PIN , OUTPUT);
  device_specific_setoutput(EEPROM.read(0xF9));

  Serial.printf("Dimmer options: %02X\n",EEPROM.read(0x8D));
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void loop(void)
{
  ESP32US140Tloop();
  
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// called in processUPB() at the beginning, so that this can take the
// place of existing commands 
// set validcommand=true, for any extra commands handled here
// (see processUPB for examples)
// return true to skip processing of existing commands if handled
// differently here
// return false to process commands as normal
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

bool device_specific_command(uint8_t UPBdata[])
{
  return false;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
// outputs the specified level, if additional hardware is present
// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void device_specific_setoutput(uint8_t dimlevel)
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
  "<a href=\"/\">&#x2302; Home</a> &nbsp; <a href=\"/edit\">&#x21bb; Refresh</a><p>\n";
  request->send(200, "text/html", reply);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

String device_specific_root(void)
{
  String reply;
  reply="Currently <b>";

  switch(EEPROM.read(0xF9))
  {
    case 0:
      reply=reply+"off";
      break;
    case 100:
      reply=reply+"on";
      break;
    default:
      reply=reply+"unknown "+String(EEPROM.read(0xF9))+"%";
      break;
  }   
  reply=reply+"</b><p>\n"
  "<a href=\"/upbcommand?cmd=22&opt=100&return=/\">On</a> &nbsp; "
  "&nbsp;<a href=\"/upbcommand?cmd=22&opt=0&return=/\">Off</a><p>\n";
  
  return reply;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void reportstatus(uint8_t who)
{
  bindata[0]=0x08;      // CTL1
  bindata[1]=EEPROM.read(0x8E) & 0b00001100;  // CTL2 includes transmission count
  bindata[2]=networkid; // NID
  bindata[3]=who;        // Dest (requesting source)
  bindata[4]=deviceid;  // Source (me)
  bindata[5]=0x86;      // Device status report
  bindata[6]=EEPROM.read(0xF9);
  
  sendbinstring(bindata);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
