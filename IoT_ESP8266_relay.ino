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
const char *mqtt_server = "mqtt";
const int mqtt_port = 1883;
WiFiClient espClient;
PubSubClient client(espClient);

// define this to silently ignore making any changes to GPIO0
#define IGNORE_GPIO0
#undef IGNORE_GPIO0

// define feature set
//#define GPIO0_RELAY
//#define GPIO0_DS18B20
//#define GPIO2_RELAY
#define GPIO2_DS18B20

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

// temperature publishing interval
#define PUBLISH_INTERVAL_MS 50*1000

#ifdef GPIO0_DS18B20
#define ONE_WIRE_BUS 0
#elif defined(GPIO2_DS18B20)
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
#elif defined(GPIO2_DS18B20)
#endif
#ifdef HAS_DS18B20
    DeviceAddress ds18b20_idx0;
#endif
    IPAddress ip;
} state_t;
state_t state;

// prototypes
void MQTT_callback(char *topic, byte *payload, unsigned int length);
void gpio0_relay(bool on);
void gpio2_relay(bool on);
String prepareFeaturesJSON(void);
#ifdef HAS_DS18B20
void printDS18B20Address(DeviceAddress deviceAddress);
void publishTemperature(void);
#endif

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
    gpio0_relay(true);
#endif
#ifdef GPIO2_RELAY
    // relay 2
    Serial.println("GPIO2 controls a relay");
    pinMode(2, OUTPUT);
    gpio2_relay(true);
#endif
#ifdef HAS_DS18B20
    DS18B20.begin();

    // locate devices on the bus
    Serial.print("locating DS18B20 devices...");
    Serial.print("found ");
    Serial.print(DS18B20.getDeviceCount(), DEC);
    Serial.println(" devices.");

    if (!DS18B20.getAddress(state.ds18b20_idx0, 0)) Serial.println("unable to retrieve address for device 0");
    Serial.println("first device address:");
    printDS18B20Address(state.ds18b20_idx0);

    DS18B20.requestTemperatures();
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
            "ESP%02X%02X", state.mac[4], state.mac[5]
        );
	Serial.print("node name: ");
	Serial.println(state.nodename);

	// register with the MQTT server
    client.setServer(mqtt_server, mqtt_port);
	client.setCallback(MQTT_callback);
}

// try to connect to server every 5 seconds until success or 5 failures occur at which point
// the ESP is reset
void reconnect() {
    int retries = 0;

    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(state.nodename)) {
            Serial.println("MQTT connection made");
		    // general reset
		    client.subscribe("/reset");
		    // node specific reset
		    client.subscribe(String("/reset/" + String(state.nodename)).c_str());
		    // features
#ifdef GPIO0_RELAY
		    client.subscribe(String("/gpio/" + String(state.nodename) + "/0").c_str());
#endif
#ifdef GPIO2_RELAY
		    client.subscribe(String("/gpio/" + String(state.nodename) + "/2").c_str());
#endif
		    client.publish("/node", prepareFeaturesJSON().c_str());
        } else {
        	Serial.println("MQTT connection failed");
            Serial.print("client state: ");
            Serial.println(client.state());
            // wait 5 seconds before retrying - note that the long delay will break stuff
            delay(5000);
            retries++;
            if (retries > 10)
                ESP.reset();
        }
    }
}

#ifdef HAS_DS18B20
long previousMillis = 0;   // last temperature update
#endif
void loop(void) {

    if (!client.connected()) {
        reconnect();
#ifdef HAS_DS18B20
        publishTemperature();
#endif
    }
	client.loop();

#ifdef HAS_DS18B20
    unsigned long currentMillis = millis();
    // check if enough time has passed to warrant publishing
    if (currentMillis - previousMillis > PUBLISH_INTERVAL_MS) {
        previousMillis = currentMillis;
        publishTemperature();
    }
#endif
}

#ifdef HAS_DS18B20
void publishTemperature(void) {
    float temp;

    // retrieve data
    DS18B20.requestTemperatures();
    temp = DallasTemperature::toFahrenheit(DS18B20.getTempC(state.ds18b20_idx0));
    Serial.println("temperature: " + String(temp));
    // publish temperature
    client.publish(String("/temperature/" + String(state.nodename)).c_str(), String(temp).c_str());
}
#endif

void MQTT_callback(char *topic, byte *payload, unsigned int length) {
    int i;
    String topicStr = String(topic);
    String payloadStr;
    payloadStr.reserve(length);

    for (i = 0; i < length; ++i)
        payloadStr += (char)payload[i];

	Serial.print(topicStr);
	Serial.print(" => ");
	Serial.println(payloadStr);
	if (topicStr == "/reset" || topicStr == "/reset/" + String(state.nodename)) {
		Serial.println("received reset request");
		yield();
		ESP.reset();
	}
#ifdef GPIO0_RELAY
	if (topicStr == "/gpio/" + String(state.nodename) + "/0") {
		Serial.print("received GPIO0 message: ");
		if (payloadStr.toInt() == 1) {
			gpio0_relay(true);
			Serial.println("OFF");
		} else {
			gpio0_relay(false);
			Serial.println("ON");
		}
	}
#endif
#ifdef GPIO2_RELAY
	if (topicStr == "/gpio/" + String(state.nodename) + "/2") {
		Serial.print("received GPIO2 message: ");
		if (payloadStr.toInt() == 1) {
			gpio2_relay(true);
			Serial.println("OFF");
		} else {
			gpio2_relay(false);
			Serial.println("ON");
		}
	}
#endif
    if (topicStr == "/node/" + String(state.nodename)) {
		Serial.print("received request for status");
        client.publish("/node", prepareFeaturesJSON().c_str());
    }
}

#ifdef HAS_DS18B20
// function to print a device address
void printDS18B20Address(DeviceAddress deviceAddress) {
    for (uint8_t i = 0; i < 8; i++) {
        // zero pad the address if necessary
        if (deviceAddress[i] < 16) Serial.print("0");
            Serial.print(deviceAddress[i], HEX);
    }
}
#endif

String prepareFeaturesJSON(void) {
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;

    String message = "{\n";
#warning the below will only work if you change MQTT_MAX_PACKET_SIZE in PubSubClient.h
#if 1
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
    message += "\t\"system\": {\n"; // includes uptime and heap free
    message += "\t\t\"uptime\": {\n\t\t\t\"hours\": " + String(hr, DEC) + ",\n\t\t\t\"minutes\": " + String(min % 60, DEC) + ",\n\t\t\t\"seconds\": " + String(sec % 60, DEC) + "\n\t\t},\n";
    message += "\t\t\"heap\": " + String(ESP.getFreeHeap(), DEC) + ",\n";
    message += "\t\t\"ip\": \"" + String(state.ip) + "\",\n";
    message += "\t},\n";   // system end (dynamic info about client)
#endif
    message += "\t\"node\": \"" + String(state.nodename) + "\",\n";
    message += "\t\"features\": [\n";
#ifdef GPIO0_RELAY
	message += "\t\t\"gpio0\",\n";
#endif
#ifdef GPIO2_RELAY
	message += "\t\t\"gpio2\",\n";
#endif
#ifdef HAS_DS18B20
	message += "\t\t\"temperature\",\n";
#endif
	message += "\t\t\"reset\"\n";
    message += "\t]\n"; // list of available features
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
