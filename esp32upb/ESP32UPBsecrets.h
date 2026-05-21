#define ESP32secretsversion 3

// WUPB devices have their own WiFi network and are VLAN'd on their own subnet, see below
#define WIFI_SSID "WUPB"
#define WIFI_PASS "wireless"

const char* wupbtopic = "home/UPBtraffic";

// MQTT server on this address
IPAddress mqttserver(192,168,123,100);
	
// subnet 239 is UPB devices

IPAddress gateway(192,168,239,1);
IPAddress netmask(255,255,255,0);

// device has static IP which matches its UPB device ID, i.e. device 50 has IP of 192.168.239.50
// starts set device ID 255, unconfigured
IPAddress localip(192,168,239,255);

// initWiFi() and refreshid() in UPBCore.h update this
