/*
ESP8266 only Internet-of-Things device

Currently supports ESP-01 w/ a relay or DS18B20 on GPIO0 and GPIO2.
The relay support is actually just driving the GPIOs with logic
    inverted.  That is, in relay mode, setting the relay to true (on)
    brings the pin low with a digialwrite.
*/

#define VERSION_MAJOR 0
#define VERSION_MINOR 4

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

#if defined(GPIO0_RELAY) && defined(GPIO0_DS18B60)
    #error "GPIO0 cannot be assigned to a relay AND a DS18B60 simultaneously"
#elif defined(GPIO2_RELAY) && defined(GPIO2_DS18B60)
    #error "GPIO2 cannot be assigned to a relay AND a DS18B60 simultaneously"
#elif defined(GPIO0_DS18B60) && defined(GPIO2_DS18B60)
    #error "GPIO0 and GPIO2 are both configured for a DS18B60; OneWire only supports a single bus"
#endif

// for the DS18B20s, we need to include the appropriate libraries
#if defined(GPIO0_DS18B60) || defined(GPIO2_DS18B60)
#include <OneWire.h>
#include <DallasTemperature.h>

#define HAS_DS18B60

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
void handleRoot(void);
void handleAPIRoot(void);
#ifdef GPIO0_RELAY
void handleRelay0On(void);
void handleRelay0Off(void);
void handleAPIRelay0On(void);
void handleAPIRelay0Off(void);
#endif
#ifdef GPIO2_RELAY
void handleRelay2On(void);
void handleRelay2Off(void);
void handleAPIRelay2On(void);
void handleAPIRelay2Off(void);
#endif
#ifdef HAS_DS18B60
void handleDS18B60(void);
void handleAPIDS18B60(void);
#endif
void handleReset(void);
void sendRootPage(void);
void sendAPIRootJSON(void);
void handleNotFound(void);

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
    server.on("/api", handleAPIRoot);
#ifdef GPIO0_RELAY
    server.on("/relay/0/on", handleRelay0On);
    server.on("/relay/0/off", handleRelay0Off);
    server.on("/api/relay/0/on", handleAPIRelay0On);
    server.on("/api/relay/0/off", handleAPIRelay0Off);
#endif
#ifdef GPIO2_RELAY
    server.on("/relay/1/on", handleRelay2On);
    server.on("/relay/1/off", handleRelay2Off);
    server.on("/api/relay/1/on", handleAPIRelay2On);
    server.on("/api/relay/1/off", handleAPIRelay2Off);
#endif
#ifdef HAS_DS18B60
    server.on("/temperature", handleTemperature);
    server.on("/api/temperature", handleAPITemperature);
#endif
    server.on("/reset", handleReset);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("HTTP server started");
}

void loop(void)
{
    mdns.update();
    server.handleClient();
}

void sendRootPage(void) {
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;

    String message = "<html>\n\t<head>\n";
#ifdef HAS_DS18B60
    message += "\t\t<meta http-equiv='refresh' content='5'/>\n";
#endif
    message += "\t\t<title>";
    message += state.hostname;
    message += "</title>\n";
    message += "\t\t<style>\n\t\t\tbody { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\n\t\t</style>\n\t</head>\n<body>\n\t\t<center>\n";
    message += "\t\t\t<p>Uptime: ";
    message += String(hr, DEC) + ":" + String(min % 60, DEC) + ":" + String(sec % 60, DEC);
    message += "</p>\n\t\t<\/center>\n\t</body>\n</html>\n";
    server.send(768, "text/html", message);
}

// prepare a full status report with the full contents of 
// state, a version, feature tags, etc.
#warning need to add uptime, heap and features to status json
void sendAPIRootJSON(void) {
    int sec = millis() / 1000;
    int min = sec / 60;
    int hr = min / 60;

    String message = "{\n\t";
    message += "\t\"version\" = {\n\t\t\"major\": " + String(VERSION_MAJOR, DEC) + ",\n\t\t\"minor\": " + String(VERSION_MINOR, DEC) +"\n\t},\n";
    message += "\t\"uptime\" = {\n\t\t\"hours\": " + String(hr, DEC) + ",\n\t\t\"minutes\": " + String(min % 60, DEC) + ",\n\t\t\"seconds\": " + String(sec % 60, DEC) + "\n\t},\n";
    message += "}";

    server.send(200, "text/json", message);
}

#ifdef GPIO0_RELAY
void handleRelay0On(void) {
    Serial.println("web turning on relay on GPIO0");
    gpio0_relay(true);
    sendRootPage();
}

void handleRelay0Off(void) {
    Serial.println("web turning off relay on GPIO0");
    gpio0_relay(false);
    sendRootPage();
}

void handleAPIRelay0On(void) {
    Serial.println("API turning on relay on GPIO0");
    gpio0_relay(true);
    sendAPIRootJSON();
}

void handleAPIRelay0Off(void) {
    Serial.println("API turning off relay on GPIO0");
    gpio0_relay(false);
    sendAPIRootJSON();
}
#endif

#ifdef GPIO2_RELAY
void handleRelay2On(void) {
    Serial.println("web turning on relay on GPIO2");
    gpio2_relay(true);
    sendRootPage();
}

void handleRelay2Off(void) {
    Serial.println("web turning off relay on GPIO2");
    gpio2_relay(false);
    sendRootPage();
}

void handleAPIRelay2On(void) {
    Serial.println("API turning on relay on GPIO2");
    gpio2_relay(true);
    sendAPIRootJSON();
}

void handleAPIRelay2Off(void) {
    Serial.println("API turning off relay on GPIO2");
    gpio2_relay(false);
    sendAPIRootJSON();
}
#endif

void handleTemperature(void) {
}

void handleAPITemperature(void) {
}

void handleRoot(void) {
    sendRootPage();
}

void handleAPIRoot(void) {
    sendAPIRootJSON();
}

void handleReset(void) {
    ESP.reset();
}

void handleNotFound(void) {
    String message = "{\n\t\"error\": \"File Not Found\",\n\t\"uri\" = \"}";
    message += server.uri();
    message += "\",\n\t\"method\": \"";
    message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
    message += "\",\n\t\"arguments\": [\n";

    for ( uint8_t i = 0; i < server.args(); i++ ) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
        message += "\t\t\"";
        message += server.argName(i);
        message += "\": \"";
        message += server.arg(i);
        message += "\",\n";
    }

    message += "\n\t]\n}";

    server.send(404, "text/json", message);
}
