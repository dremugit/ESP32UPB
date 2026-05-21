

// logs to MQTT on home/PIMemulog (change in logthis)
// 0 is quite, increasing to see more of what goes on
// up to 3 will show each pulse which will prolly be so busy as prevent UPStart from working
#define DEBUG_LEVEL   0

#define UPStartPort Serial // built-in port used for programming

// remember not to use printableprint or other functions that use Serial.print
// since Serial is the UPStart interface
#include "ESP32UPBdisplay.h"
#include "ESP32UPBsecrets.h"

#include <WiFi.h>


#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient mqttclient(espClient); 
uint32_t lastReconnectAttempt = 0;


char mqttmsg[100]={0};

bool mqttsend=false;
bool sendtoUPStart=false;

uint8_t mqttlen=0;

// these emulate the Pulseworx PIM-R v5.57

byte registers[] = {
0xFF,0xFB,0x12,0x34,0x03,0x03,0x00,0x01,0x00,0x30,0x05,0x57,0x00,0x00,0x00,0x00, // 00-0F
0x4E,0x65,0x77,0x20,0x4E,0x65,0x74,0x77,0x6F,0x72,0x6B,0x20,0x4E,0x61,0x6D,0x65, // 10-1F
0x4E,0x65,0x77,0x20,0x52,0x6F,0x6F,0x6D,0x20,0x4E,0x61,0x6D,0x65,0x20,0x20,0x20, // 20-2F
0x4E,0x65,0x77,0x20,0x50,0x49,0x4D,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20, // 30-3F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 40-4F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 50-5F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 60-6F
0x02,0xFC,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 70-7F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 80-8F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 90-9F
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // A0-AF
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // B0-BF
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // C0-CF
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // D0-DF
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // E0-EF
0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x0D,0x00,0x00,0x00,0x00,0x00,0x00, // F0-FF
};

char sendstring[100];
uint8_t packetlength;
uint8_t ct2;
uint8_t ackreq;
uint8_t xmitcount;
uint8_t xmitseq;

//uint8_t dst;
uint8_t mdid;
uint8_t src;
uint8_t regnum, regqty;

char intermediate[100];
char pulsesendstring[100];

char temp[100];

uint8_t bitmask[]={
0b11000000,
0b00110000,
0b00001100,
0b00000011
};

char quatlooshi[]={'0','0','0','0','1','1','1','1','2','2','2','2','3','3','3','3'};
char quatlooslo[]={'0','1','2','3','0','1','2','3','0','1','2','3','0','1','2','3'};

char bitdata[5];
uint8_t hexdata;
uint8_t seq;
uint32_t lastUPStartpulse=0;

char UPStartData[100]={0};
uint8_t numbytes=0;

// send a pulse every now and again to keep UPStart happy
#define KEEPALIVE_SPACING   1000 // was 5000

void setup()
{
  pinMode(2, OUTPUT);
  // static IP on same subnet as WUPB devices
  // can be elsewhere if that subnet is routed
  localip={192,168,239,252};
  
  if (!WiFi.config(localip, gateway, netmask))
  {
    // WiFi failed, which is bad, but can't log to MQTT, can't print to Serial
    // so LED just blinks to indicate error
    while (true)
    {
      digitalWrite(2, HIGH);
      delay(500);
      digitalWrite(2, LOW);
      delay(500);
    }
  }
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
  }
  
  mqttclient.setServer(mqttserver, 1883);
  mqttclient.setCallback(mqttcallback);

  // spin MQTT for a bit
  waitformqttconnect(5000);
  // so this can be published
  logthis("ASIB ",__FILE__);
  logthis("     ",__TIME__);
  logthis("     ",__DATE__);
  sprintf(temp,"Debug level %d",DEBUG_LEVEL);
  logthis("     ",temp);
  
  UPStartPort.begin(4800);
  
  // not necessarily true, but indicates running to UPStart
  UPStartPort.print("PA\x0D");
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void loop()
{
  waitformqttconnect(1);

  // leaving as little code in the actual subscribe as possible
  if (mqttlen)
  {
    // copy to intermediate as mqttmsg gets hosed somehow
    memcpy(intermediate,mqttmsg,mqttlen+1);

    // process incoming data from device to make WUPB more like a line device
    ct2=hexcharstobyte(intermediate[2],intermediate[3]);
    ackreq=(ct2 & 0b01110000) >> 4;  
    src=hexcharstobyte(intermediate[8],intermediate[9]);
    mdid=hexcharstobyte(intermediate[10],intermediate[11]);

    sendtoUPStart=true;

    // only customize pulse return for ack packets from WUPB devices
    if (mdid==0x80)
    {
      // if ID pulses specified, send them
      if ((ackreq & 0b010) == 0b010)
      {
        sendtoUPStart=false;
        sendidpulses(src);
        #if DEBUG_LEVEL > 0
          sprintf(temp,"Sent ID pulses for device %d",src);
          logthis("Info ",temp);
        #endif
      }
  
      // if ack pulse specified, send it
      if ((ackreq & 0b001) == 0b001)
      {
        sendtoUPStart=false;
        UPStartPort.print("AFF\x0D");
        #if DEBUG_LEVEL > 0
          sprintf(temp,"Sent ack pulse for device %d",src);
          logthis("Info ",temp);
        #endif
      }
    }

    #if DEBUG_LEVEL > 0
      logthis("MQrx ",intermediate);
    #endif

    if (sendtoUPStart)
    {
      // if in pulse mode
      if ((registers[0x70] & 0b00000010) == 0b00000000)
      {
        #if DEBUG_LEVEL > 0
          sprintf(temp,"Quatting to UPStart %s",intermediate);
          logthis("Info ",temp);
        #endif
        sendpulsestring(intermediate);
      }
      // if in message mode
      else
      {
        #if DEBUG_LEVEL > 0
          sprintf(temp,"Messaging to UPStart %s",intermediate);
          logthis("Info ",temp);
        #endif
        UPStartPort.print("PU");
        UPStartPort.print(intermediate);
        UPStartPort.print("\x0D");
      }
    } 
 
    mqttlen=0;
  }





  
  // if in pulse mode, send a pulse every while to placate UPStart
  // could also or instead send --F if bit 0 is low
  // as a keepalive
  if ((registers[0x70] & 0b00000010) == 0b00000000)
  {
    if ((millis()-lastUPStartpulse) > KEEPALIVE_SPACING)
    {
      UPStartPort.print("X0F\x0D");
      lastUPStartpulse=millis();
    }
  }
  
  if (UPStartPort.available())
  {
    numbytes=UPStartPort.readBytesUntil('\x0D',UPStartData,100);
    UPStartData[numbytes]=0x00;

    #if DEBUG_LEVEL > 0
      logthis("PC   ",UPStartData);
    #endif
    
    if ((UPStartData[0]==0x12) || (UPStartData[0]=='R')) // ^R read register(s)
    {
      regnum=hexcharstobyte(UPStartData[1],UPStartData[2]);
      regqty=hexcharstobyte(UPStartData[3],UPStartData[4]);

      UPStartPort.printf("PR%02X",regnum);
      for (uint8_t reg=0; reg<regqty; reg++)
      {
        UPStartPort.printf("%02X",registers[regnum+reg]);
      }
      UPStartPort.print("\x0D");
    }

    if ((UPStartData[0]==0x17) || (UPStartData[0]=='W')) // ^W write register(s)
    {
      regnum=hexcharstobyte(UPStartData[1],UPStartData[2]);
      regqty=(numbytes-5)/2;

      for (uint8_t reg=0; reg<regqty; reg++)
      {
        registers[regnum+reg]=hexcharstobyte(UPStartData[3+2*reg],UPStartData[4+2*reg]);
      }

      UPStartPort.print("PA\x0D");
    }

    if ((UPStartData[0]==0x14) || (UPStartData[0]=='T'))  // ^T transmit this command
    {
      // this is the only one that goes to the line PIM
      // so it stays in pulse-but-no-idle mode
      // new for passthru
      logthis("UPst ",UPStartData);

      UPStartPort.print("PA\x0D");

      // note shifted because of ^T
      packetlength=hexcharstobyte(UPStartData[1],UPStartData[2]) & 0b00011111;

      // shift off ^T
      for (uint8_t i=0; i<2*packetlength; i++)
      {
        intermediate[i]=UPStartData[i+1];
        sendstring[i]=UPStartData[i+1];
      }
      intermediate[2*packetlength]=0x00;
      sendstring[2*packetlength]=0x00;

      ct2=hexcharstobyte(intermediate[2],intermediate[3]);
      ackreq=(ct2 & 0b01110000) >> 4;  
      xmitcount=1+((ct2 & 0b00001100) >> 2);  
//      dst=hexcharstobyte(intermediate[6],intermediate[7]);
//      mdid=hexcharstobyte(intermediate[10],intermediate[11]);

      mqttsend=true;

      for (uint8_t i=0; i<xmitcount; i++)
      {
        // recalculate CT2 for each transmit sequence
        uint8_t newct2=(ct2 & 0b11111100) | i;
        sprintf(temp,"%X\x00",newct2&0x0F);
        intermediate[3]=temp[0];
        uint8_t newcsum=UPBchecksum(intermediate);
        sprintf(temp,"%02X\x0D\x00",newcsum);
        intermediate[packetlength*2-2]=temp[0];
        intermediate[packetlength*2-1]=temp[1];
        xmitpulses(intermediate);
      }      

      if (mqttsend)
      {
        #if DEBUG_LEVEL > 0
          logthis("MQtx ",sendstring);
        #endif
        if (mqttclient.publish("home/UPBtraffic", sendstring)==false)
        {
          // this seems unlikely to work
          // if one publish failed, another will prolly too
          logthis("     ","MQTT publish failure");
        }
      }
    }
  }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void sendpulses(char pulsedata[])
{
  ct2=hexcharstobyte(pulsedata[2],pulsedata[3]);
  ackreq=(ct2 & 0b01110000) >> 4;  

  seq=0;

  UPStartPort.print("XFF\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Quat ","XFF\x0D");
  #endif
  UPStartPort.print("RFF\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Quat ","RFF\x0D");
  #endif

  for (uint8_t quatbit=0; quatbit<strlen(pulsedata); quatbit++)
  {
    hexdata=hexchartobyte(pulsedata[quatbit]);
    sprintf(bitdata,"%cF%X\x0D\x00",quatlooshi[hexdata],seq);
    UPStartPort.print(bitdata);
    #if DEBUG_LEVEL > 2
      logthis("Quat ",bitdata);
    #endif
    seq++; if (seq==0x10) {seq=0;}
    sprintf(bitdata,"%cF%X\x0D\x00",quatlooslo[hexdata],seq);
    UPStartPort.print(bitdata);
    #if DEBUG_LEVEL > 2
      logthis("Quat ",bitdata);
    #endif
    seq++; if (seq==0x10) {seq=0;}
  }

  if (ackreq==0)
  {
    UPStartPort.print("N-F\x0D");
    #if DEBUG_LEVEL > 2
      logthis("Quat ","N-F\x0D");
    #endif
  }

  // this is a bit of a hack as it assumes there's a device on the far end
  // but MQTT timing might mean the 0x80 is delayed and can't be converted
  // to and ack pulse in time
  if ((ackreq & 0b001) == 0b001)
  {
    UPStartPort.print("AFF\x0D");
    #if DEBUG_LEVEL > 2
      logthis("Quat ","AFF\x0D");
    #endif
  }
  lastUPStartpulse=millis();

}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void xmitpulses(char pulsedata[])
{
  ct2=hexcharstobyte(pulsedata[2],pulsedata[3]);
  ackreq=(ct2 & 0b01110000) >> 4;  

  // preamble 2112 == XXRR
  UPStartPort.print("T20\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Xmit ","T20\x0D");
  #endif
  UPStartPort.print("T11\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Xmit ","T11\x0D");
  #endif
  UPStartPort.print("T12\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Xmit ","T12\x0D");
  #endif
  UPStartPort.print("T23\x0D");
  #if DEBUG_LEVEL > 2
    logthis("Xmit ","T23\x0D");
  #endif

  // bump because preamble has already been transmitted
  seq=4;

  for (uint8_t quatbit=0; quatbit<strlen(pulsedata); quatbit++)
  {
    hexdata=hexchartobyte(pulsedata[quatbit]);
    sprintf(bitdata,"T%c%X\x0D\x00",quatlooshi[hexdata],seq);
    UPStartPort.print(bitdata);
    #if DEBUG_LEVEL > 2
      logthis("Xmit ",bitdata);
    #endif
    seq++; if (seq==0x10) {seq=0;}
    sprintf(bitdata,"T%c%X\x0D\x00",quatlooslo[hexdata],seq);
    UPStartPort.print(bitdata);
    #if DEBUG_LEVEL > 2
      logthis("Xmit ",bitdata);
    #endif
    seq++; if (seq==0x10) {seq=0;}
  }

  // if ack pulse requested
  if ((ackreq&0b001)==0b001)
  {
    UPStartPort.print("AFF\x0D");
    #if DEBUG_LEVEL > 2
      logthis("Xmit ","AFF\x0D");
    #endif
  }
  lastUPStartpulse=millis();

}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void sendidpulses(uint8_t deviceid)
{
  char idpulse[5];
  char idhex[2];
  idpulse[3]=0x0d;
  idpulse[4]=0x00;

  for (uint16_t dev=0; dev<256; dev++)
  {
    sprintf(idhex,"%X\x00",dev & 0x0F);
    idpulse[2]=idhex[0];
    if (dev==deviceid)
    {
      idpulse[0]='A';
      idpulse[1]='F';
    }
    else
    {
      idpulse[0]='N';
      idpulse[1]='-';
    }
    UPStartPort.print(idpulse);

    #if DEBUG_LEVEL > 3
      char idstring[10];
      sprintf(idstring,"%02X   \x00",dev);
      logthis(idstring,idpulse);
    #endif
    
  }
 
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void logthis(char who[], char something[])
{
  // change this up here
  // so it look nice in log
  // but gets processed natively
  switch (something[0])
  {
    case 0x12: // ^R read register(s)
      something[0]='R';
      break;
    case 0x14: // ^T transmit this data
      something[0]='T';
      break;
    case 0x17: // ^W write register(s)
      something[0]='W';
      break;
    default:
      break;
  }
  
  char pubstring[150]={0};
  strcat(pubstring,who);
  strcat(pubstring,something);

  mqttclient.publish("home/PIMemulog", pubstring);
  
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

uint8_t UPBchecksum(char UPBcommand[])
{
  // lower five bits of CTL1 are packet length
  uint8_t packetlength=hexcharstobyte(UPBcommand[0],UPBcommand[1]) & 0b00011111;

  uint32_t total=0;

  for (int i=0; i<(packetlength-1)*2; i+=2)
  {
    total+=hexcharstobyte(UPBcommand[i],UPBcommand[i+1]);
  }

  return (256-(total & 0xFF)) & 0xFF;
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

void sendpulsestring(char pulsestring[])
{
  // make a copy
  // manipulating the one passed in is bad string juju

  memcpy(pulsesendstring,pulsestring,strlen(pulsestring));
  packetlength=hexcharstobyte(pulsesendstring[0],pulsesendstring[1]) & 0b00011111;

  ct2=hexcharstobyte(pulsesendstring[2],pulsesendstring[3]);
  ackreq=(ct2 & 0b01110000) >> 4;  
  xmitcount=1+((ct2 & 0b00001100) >> 2);  

  for (uint8_t xmitseq=0; xmitseq<xmitcount; xmitseq++)
  {
    // recalculate CT2 for each transmit sequence
    uint8_t newct2=(ct2 & 0b11111100) | xmitseq;
    sprintf(temp,"%X\x00",newct2&0x0F);
    pulsesendstring[3]=temp[0];
    uint8_t newcsum=UPBchecksum(pulsesendstring);
    sprintf(temp,"%02X\x0D\x00",newcsum);
    pulsesendstring[packetlength*2-2]=temp[0];
    pulsesendstring[packetlength*2-1]=temp[1];
    pulsesendstring[packetlength*2]=0x00;
    sendpulses(pulsesendstring);
  }
}

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

boolean reconnect()
{
  if (mqttclient.connect("WUPBPIMemu"))
  {
    // Once connected, (re)subscribe
    mqttclient.subscribe("home/UPBtraffic");
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

// -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
