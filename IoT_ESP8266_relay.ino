/*
ESP8266 only Internet-of-Things device

Currently supports ESP-01 w/ a relay or DS18B20 on GPIO0 and GPIO2.
The relay support is actually just driving the GPIOs with logic
	inverted.  That is, in relay mode, setting the relay to true (on)
	brings the pin low with a digialwrite.
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/* wificred.h contains something like:
   const char* ssid = ".........";
   const char* password = ".........";
*/
#include "wificred.h"

// define this to silently ignore making any changes to GPIO0
#define IGNORE_GPIO0
#undef IGNORE_GPIO0

// define feature set
#define GPIO0_RELAY
//#define GPIO0_DS18B60
#define GPIO2_RELAY
//#define GPIO2_DS18B60

#ifdef defined(GPIO0_RELAY) && defined(GPIO0_DS18B60)
	#error "GPIO0 cannot be assigned to a relay AND a DS18B60 simultaneously"
#elif defined(GPIO2_RELAY) && defined(GPIO2_DS18B60)
	#error "GPIO2 cannot be assigned to a relay AND a DS18B60 simultaneously"
#elif defined(GPIO0_DS18B60) && defined(GPIO2_DS18B60)
	#error "GPIO0 and GPIO2 are both configured for a DS18B60; OneWire only supports a single bus"
#endif

// for the DS18B20s, we need to include the appropriate libraries
#ifdef defined(GPIO0_DS18B60) || defined(GPIO2_DS18B60)
#include <OneWire.h>
#include <DallasTemperature.h>

#ifdef GPIO0_DS18B60
#define ONE_WIRE_BUS 0
#elif GPIO2_DS18B60
#define ONE_WIRE_BUS 2
#else
#error "unknown GPIO pin for OneWire bus"
#endif
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
#endif

#define HOSTNAME_MAX_LENGTH 12

// system state
typedef struct stateStruct {
	byte mac[6];
	char hostname[HOSTNAME_MAX_LENGTH];
	uint32_t chipId;
	uint32_t flashChipId;
	uint32_t flashChipSize;
	uint32_t flashChipSpeed;
#ifdef GPIO0_RELAY
	bool gpio0_relay;
#elif GPIO0_DS18B60
#endif
#ifdef GPIO2_RELAY
	bool gpio2_relay;
#elif GPIO2_DS18B60
#endif
	IPAddress ip;
} state_t;
state_t state;

// prototypes
void handleRoot();
#ifdef GPIO0_RELAY
void handleRelay0On(void);
void handleRelay0Off(void);
#endif
#ifdef GPIO2_RELAY
void handleRelay2On(void);
void handleRelay2Off(void);
#endif
void handleNotFound(void);
void sendIndexPage(void);

// multicast DNS responder
MDNSResponder mdns;

// Webserver on port 80
ESP8266WebServer server(80);

#ifdef GPIO0_RELAY
void gpio0_relay(bool on) {
    state.gpio0_relay = on;
#ifndef IGNORE_GPIO0
	if (on) {
    	digitalWrite(0, LOW);
	} else {
    	digitalWrite(0, HIGH);
	}
#endif
}
#endif

#ifdef GPIO2_RELAY
void gpio2_relay(bool on) {
    state.gpio2_relay = on;
	if (on) {
    	digitalWrite(2, LOW);
	} else {
    	digitalWrite(2, HIGH);
	}
}
#endif

// setup the output serial port (used for debugging)
// connect to the wifi AP
// setup and start the mDNS responder with hostname ESP_XXYYZZ 
//      where XXYYZZ are the lowercase upper bytes of the MAC address
// setup and start the webserver
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
    snprintf(state.hostname, HOSTNAME_MAX_LENGTH,
            "ESP%X%X%X", state.mac[5], state.mac[4], state.mac[3]
        );
    // register hostname w/ mDNS 
    Serial.println("");
    if (!mdns.begin(state.hostname, state.ip)) {
        Serial.println("Error setting up MDNS responder!");
        while(1) { 
            delay(500);
            Serial.print(".");
        }
    }
    Serial.print("mDNS responder started with hostname: ");
    Serial.println(state.hostname);

    // setup web server
    server.on("/", handleRoot);
#ifdef GPIO0_RELAY
    server.on("/relay/0/on", handleRelay0On);
    server.on("/relay/0/off", handleRelay0Off);
#endif
#ifdef GPIO2_RELAY
    server.on("/relay/1/on", handleRelay2On);
    server.on("/relay/1/off", handleRelay2Off);
#endif
	server.onNotFound(handleNotFound);
	server.begin();
    Serial.println("HTTP server started");
}

void loop(void)
{
    mdns.update();
    server.handleClient();
}

void sendIndexPage(void) {
	char temp[768];
	int sec = millis() / 1000;
	int min = sec / 60;
	int hr = min / 60;

	snprintf ( temp, 768,

"<html>\
  <head>\
    <meta http-equiv='refresh' content='30'/>\
    <title>%s</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <center>\
        <p>Uptime: %02d:%02d:%02d</p>\
        <hr>\
        <h1>Relay 0</h1>\
        <br>\
        <table><tr><td>%s<a href=\"/relay/0/on\">On</a>%s</td><td>%s<a href=\"/relay/0/off\">Off</a>%s</td></table>\
        <hr>\
        <h1>Relay 1</h1>\
        <br>\
        <table><tr><td>%s<a href=\"/relay/1/on\">On</a>%s</td><td>%s<a href=\"/relay/1/off\">Off</a>%s</td></table>\
    </center>\
  </body>\
</html>",
		state.hostname,
        hr, min % 60, sec % 60,
        (state.gpio0_relay ? "<b>" : ""), (state.gpio0_relay ? "</b>" : ""), (state.gpio0_relay ? "" : "<b>"), (state.gpio0_relay ? "" : "</b>"),
        (state.gpio2_relay ? "<b>" : ""), (state.gpio2_relay ? "</b>" : ""), (state.gpio2_relay ? "" : "<b>"), (state.gpio2_relay ? "" : "</b>")
	);
	server.send(768, "text/html", temp);
}

#ifdef GPIO0_RELAY
void handleRelay0On(void) {
    Serial.println("turning on relay on GPIO0");
    gpio0_relay(true);
    sendIndexPage();
}

void handleRelay0Off(void) {
    Serial.println("turning off relay on GPIO0");
    gpio0_relay(false);
    sendIndexPage();
}
#endif

#ifdef GPIO2_RELAY
void handleRelay2On(void) {
    Serial.println("turning on relay on GPIO2");
    gpio2_relay(true);
    sendIndexPage();
}

void handleRelay2Off(void) {
    Serial.println("turning off relay on GPIO2");
    gpio2_relay(false);
    sendIndexPage();
}
#endif

void handleRoot(void) {
    sendIndexPage();
}

void handleNotFound(void) {
	String message = "File Not Found\n\n";
	message += "URI: ";
	message += server.uri();
	message += "\nMethod: ";
	message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
	message += "\nArguments: ";
	message += server.args();
	message += "\n";

	for ( uint8_t i = 0; i < server.args(); i++ ) {
		message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
	}

	server.send ( 404, "text/plain", message );
}
