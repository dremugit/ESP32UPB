#define DEBUG_LEVEL   1

#include "ESP32UPBdisplay.h"
#include "ESP32UPBsecrets.h"

#define PIMPort Serial1

#include <WiFi.h>

#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient mqttclient(espClient); 
uint32_t lastReconnectAttempt = 0;

char PIMData[100]={0};
uint8_t numbytes=0;
char mqttmsg[100]={0};
bool mqttsend=false;
uint8_t mqttlen=0;
uint8_t packetlength;
char intermediate[100];

uint8_t ct2;
uint8_t wupb;

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nAnd so it begins");
  Serial.println(__FILE__);
  Serial.print(__TIME__);
  Serial.print(F(" "));
  Serial.println(__DATE__);

// static IP configuration
// uses device ID 252, reserved device
// don't need internet access, so no DNS
// does have gateway in case MQTT is on a different subnet
  localip={192,168,239,252};

  if (!WiFi.config(localip, gateway, netmask))
  {
    Serial.println("Couldn't init WiFi");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }

  // spin MQTT for a bit
  waitformqttconnect(3000);

  mqttclient.setServer(mqttserver, 1883);
  mqttclient.setCallback(mqttcallback);

  PIMPort.begin(4800, SERIAL_8N1, 19, 18);  // Rx on 19, Tx on 18

  // set to message mode
  // send twice in case of noise or the serial port not syncing at first
  pimregset(0x70,0x02);
  pimregset(0x70,0x02);
  Serial.println("Ready");
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void loop()
{
  // 1 means just looping, no long delay
  waitformqttconnect(1);

  if (PIMPort.available())
  {
    numbytes=PIMPort.readBytesUntil('\x0D',PIMData,100);
    PIMData[numbytes]=0x00;

    #if DEBUG_LEVEL > 1
      Serial.printf("PIM said %s\n",PIMData);
    #endif

    if ((PIMData[0]=='P') && (PIMData[1]=='U'))
    {
      mqttsend=true;
      // if received WUPB data on line (C2H==1)
      // don't re-publish to WUPB, as those packets were sent by this sketch and would infinitely loop
      ct2=hexchartobyte(PIMData[4]);
      wupb=(ct2 & 0b1000) >> 3;  

      if (wupb==1)
      {
        mqttsend=false;
      }

      if (mqttsend)
      {
        // note shifted because of PU
        packetlength=hexcharstobyte(PIMData[2],PIMData[3]) & 0b00011111;
    
        // shift off PU
        for (uint8_t i=0; i<2*packetlength; i++)
        {
          intermediate[i]=PIMData[i+2];
        }
        intermediate[2*packetlength]=0x00;

        #if DEBUG_LEVEL > 0
          Serial.printf("Line -> MQTT %s\n",intermediate);
        #endif
          
        if (mqttclient.publish(wupbtopic, intermediate)==false)
        {
          Serial.println("MQTT publish failure");
        }
      } 
    }
  }  
    
  // leaving as little code in the actual subscribe as possible
  if (mqttlen)
  {
    // if received line data on WUPB (C2H==0)
    // don't re-send to line (again, infinite loop)
    ct2=hexchartobyte(mqttmsg[2]);
    wupb=(ct2 & 0b1000) >> 3;  

    if (wupb==0)
    {
      mqttlen=0;
    }
  }
  
  if (mqttlen)
  {
    // mqttmsg[] isn't quite right, has crap on the end
    mqttmsg[mqttlen]=0x00;

    #if DEBUG_LEVEL > 0
      Serial.printf("MQTT -> line %s\n",mqttmsg);
    #endif

    uint8_t ctlt=0x14;
    PIMPort.write(ctlt);
    PIMPort.write(mqttmsg);
    PIMPort.write("\x0D");

    mqttlen=0;
  }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void pimregset(uint8_t regnum, uint8_t regval)
{
  uint8_t ctlw=0x17;
  PIMPort.write(ctlw);

  char rnstr[3];
  sprintf(rnstr,"%02X",regnum);

  char rvstr[3];
  sprintf(rvstr,"%02X",regval);

  // only init four characters for checksum
  char prsstr[]="rnrs\x00\x00\x00\x00";
  prsstr[0]=rnstr[0];
  prsstr[1]=rnstr[1];
  prsstr[2]=rvstr[0];
  prsstr[3]=rvstr[1];

  uint32_t total=0;

  for (uint8_t i=0; i<4; i+=2)
  {
    total+=hexcharstobyte(prsstr[i],prsstr[i+1]);
  }

  uint8_t pimcsum=(256-(total & 0xFF)) & 0xFF;

  char csstr[3];
  sprintf(csstr,"%02X\x00",pimcsum);
  // then add others after calculating checksum
  prsstr[4]=csstr[0];
  prsstr[5]=csstr[1];
  prsstr[6]=0x0d;
  prsstr[7]=0x00;

  PIMPort.write(prsstr);
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

boolean reconnect()
{
  if (mqttclient.connect("WUPBbridge"))
  {
    // Once connected, (re)subscribe
    mqttclient.subscribe(wupbtopic);
  }
  return mqttclient.connected();
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void mqttcallback(char* topic, byte* payload, uint16_t msglength)
{
  memcpy(mqttmsg,payload,msglength);
  mqttlen=msglength;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void waitformqttconnect(uint32_t cycletime)
{
  uint32_t starttime=millis();
  
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
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
