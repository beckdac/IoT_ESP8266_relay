/*
ESP8266 only Internet-of-Things device
*/

#define VERSION_MAJOR 0
#define VERSION_MINOR 5

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

/* wificred.h contains something like:
   const char* ssid = ".........";
   const char* password = ".........";
*/
#include "wificred.h"

// MQTT server
IPAddress server(192, 168, 1, 1);
PubSubClient client(server);

// define this to silently ignore making any changes to GPIO0
#define IGNORE_GPIO0
#undef IGNORE_GPIO0

// define feature set
#define GPIO0_RELAY
//#define GPIO0_DS18B20
#define GPIO2_RELAY
//#define GPIO2_DS18B20

#if defined(GPIO0_RELAY) && defined(GPIO0_DS18B20)
    #error "GPIO0 cannot be assigned to a relay AND a DS18B20 simultaneously"
#elif defined(GPIO2_RELAY) && defined(GPIO2_DS18B20)
    #error "GPIO2 cannot be assigned to a relay AND a DS18B20 simultaneously"
#elif defined(GPIO0_DS18B20) && defined(GPIO2_DS18B20)
    #error "GPIO0 and GPIO2 are both configured for a DS18B20; OneWire only supports a single bus"
#endif

// for the DS18B20s, we need to include the appropriate libraries
#if defined(GPIO0_DS18B20) || defined(GPIO2_DS18B20)
#include <OneWire.h>
#include <DallasTemperature.h>

#define HAS_DS18B20

#ifdef GPIO0_DS18B20
#define ONE_WIRE_BUS 0
#elif GPIO2_DS18B20
#define ONE_WIRE_BUS 2
#else
#error "unknown GPIO pin for OneWire bus"
#endif
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
#endif

#define NODENAME_MAX_LENGTH 12

// system state
typedef struct stateStruct {
    byte mac[6];
    char nodename[NODENAME_MAX_LENGTH];
    uint32_t chipId;
    uint32_t flashChipId;
    uint32_t flashChipSize;
    uint32_t flashChipSpeed;
#ifdef GPIO0_RELAY
    bool gpio0_relay;
#elif GPIO0_DS18B20
#endif
#ifdef GPIO2_RELAY
    bool gpio2_relay;
#elif GPIO2_DS18B20
#endif
#ifdef HAS_DS18B20
#endif
    IPAddress ip;
} state_t;
state_t state;

// prototypes
void MQTT_callback(const MQTT::Publish& pub);
void gpio0_relay(bool on);
void gpio2_relay(bool on);
String prepareFeaturesJSON(void);

// setup the output serial port (used for debugging)
// connect to the wifi AP
// setup and start the mqtt client
void setup(void)
{    
    Serial.begin(115200);

    // setup the basic state structure
    state.chipId = ESP.getChipId();
    state.flashChipId = ESP.getFlashChipId();
    state.flashChipSize = ESP.getFlashChipSize();
    state.flashChipSpeed = ESP.getFlashChipSpeed();

    // initialize pins features
#ifdef GPIO0_RELAY
    // relay 0
    Serial.println("GPIO0 controls a relay");
#ifndef IGNORE_GPIO0
    pinMode(0, OUTPUT);
#endif
    gpio0_relay(false);
#endif
#ifdef GPIO2_RELAY
    // relay 2
    Serial.println("GPIO2 controls a relay");
    pinMode(2, OUTPUT);
    gpio2_relay(false);
#endif

    // connect to WiFi network
    WiFi.begin(ssid, password);
    Serial.println("");    
    
    // wait for connection
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(ssid);
    Serial.print("IP address: ");
    state.ip = WiFi.localIP();
    Serial.println(state.ip);
    // get mac address and assemble hostname for mDNS
    WiFi.macAddress(state.mac);
    snprintf(state.nodename, NODENAME_MAX_LENGTH,
            "ESP%X%X", state.mac[5], state.mac[4]
        );
	Serial.print("node name: ");
	Serial.println(state.nodename);

	// register with the MQTT server
	client.set_callback(MQTT_callback);
	if (client.connect(state.nodename)){
		// general reset
		client.subscribe("/reset");
		// node specific reset
		client.subscribe("/reset/" + String(state.nodename));
		// features
#ifdef GPIO0_RELAY
		client.subscribe("/gpio/" + String(state.nodename) + "/0");
#endif
#ifdef GPIO2_RELAY
		client.subscribe("/gpio/" + String(state.nodename) + "/2");
#endif
		client.publish("/node", prepareFeaturesJSON());
	} else {
    	Serial.println("MQTT connection failed");
		// wait a minute and restart - note that the long delay will break stuff
		delay(60000);
		ESP.reset();
	}
    Serial.println("MQTT connection made");
}

void loop(void) {
	client.loop();
}

void MQTT_callback(const MQTT::Publish& pub) {
	Serial.print(pub.topic());
	Serial.print(" => ");
	Serial.println(pub.payload_string());
	if (pub.topic() == "/reset" || pub.topic() == "/reset/" + String(state.nodename)) {
		Serial.println("Received reset request");
		yield();
		ESP.reset();
	}
#ifdef GPIO0_RELAY
	if (pub.topic() == "/gpio/" + String(state.nodename) + "/0") {
		Serial.print("Received GPIO0 message: ");
		if (pub.payload_string().toInt() == 1) {
			gpio0_relay(true);
			Serial.println("ON");
		} else {
			gpio0_relay(false);
			Serial.println("OFF");
		}
	}
#endif
#ifdef GPIO2_RELAY
	if (pub.topic() == "/gpio/" + String(state.nodename) + "/2") {
		Serial.print("Received GPIO2 message: ");
		if (pub.payload_string().toInt() == 1) {
			gpio2_relay(true);
			Serial.println("ON");
		} else {
			gpio2_relay(false);
			Serial.println("OFF");
		}
	}
#endif
}

String prepareFeaturesJSON(void) {
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;

    String message = "{\n";
#if 0
    message += "\t\"version\" = {\n\t\t\"major\": " + String(VERSION_MAJOR, DEC) + ",\n\t\t\"minor\": " + String(VERSION_MINOR, DEC) +"\n\t},\n";
    message += "\t\"info\" = {\n";
    message += "\t\t\"chipID\": " + String(state.chipId, DEC) + ",\n";
    message += "\t\t\"flashChipId\": " + String(state.flashChipId, DEC) + ",\n";
    message += "\t\t\"flashChipSize\": " + String(state.flashChipSize, DEC) + ",\n";
    message += "\t\t\"flashChipSpeed\": " + String(state.flashChipSpeed, DEC) + ",\n";
    message += "\t\t\"MAC\": \"" +
            String(state.mac[0], HEX) + ":" +\
            String(state.mac[1], HEX) + ":" +\
            String(state.mac[2], HEX) + ":" +\
            String(state.mac[3], HEX) + ":" +\
            String(state.mac[4], HEX) + ":" +\
            String(state.mac[5], HEX) + \
        "\",\n";
    message += "\t},\n"; // info (static info about client)
    message += "\t\"system\" = {\n"; // includes uptime and heap free
    message += "\t\t\"uptime\" = {\n\t\t\t\"hours\": " + String(hr, DEC) + ",\n\t\t\t\"minutes\": " + String(min % 60, DEC) + ",\n\t\t\t\"seconds\": " + String(sec % 60, DEC) + "\n\t\t},\n";
    message += "\t\t\"heap\" = " + String(ESP.getFreeHeap(), DEC) + ",\n";
    message += "\t\t\"ip\" = \"" + String(state.ip) + "\",\n";
    message += "\t},\n";   // system end (dynamic info about client)
#endif
    message += "\t\"node\": { \"" + String(state.nodename) + "\" },\n";
    message += "\t\"features\" = [\n";
	message += "\t\t\"reset\",\n";
#ifdef GPIO0_RELAY
	message += "\t\t\"gpio0\",\n";
#endif
#ifdef GPIO2_RELAY
	message += "\t\t\"gpio2\",\n";
#endif
#ifdef HAS_DS18B20
	message += "\t\t\"temperature\",\n";
#endif
    message += "\t],\n"; // list of available features
    message += "}";

    //Serial.println(message);

	return message;
}

#ifdef GPIO0_RELAY
void gpio0_relay(bool on) {
    state.gpio0_relay = on;
#ifndef IGNORE_GPIO0
    if (on) {
        digitalWrite(0, HIGH);
    } else {
        digitalWrite(0, LOW);
    }
#endif
}
#endif

#ifdef GPIO2_RELAY
void gpio2_relay(bool on) {
    state.gpio2_relay = on;
    if (on) {
        digitalWrite(2, HIGH);
    } else {
        digitalWrite(2, LOW);
    }
}
#endif
