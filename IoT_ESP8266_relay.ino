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

// prototypes
void handleRoot();
void handleRelay0On(void);
void handleRelay0Off(void);
void handleRelay1On(void);
void handleRelay1Off(void);
void handleNotFound(void);
void sendIndexPage(void);

// MAC address of this unit
byte mac[6];
// mDNS name
const int MDNS_NAME_MAX_LENGTH = 12;
char mDNSName[MDNS_NAME_MAX_LENGTH];
// state of the relays
bool relay0 = false, relay1 = false;

// multicast DNS responder
MDNSResponder mdns;

// Webserver on port 80
ESP8266WebServer server(80);

void relay0On(void) {
    relay0 = true;
#ifndef IGNORE_GPIO0
    digitalWrite(0, LOW);
#endif
}

void relay0Off(void) {
    relay0 = false;
#ifndef IGNORE_GPIO0
    digitalWrite(0, HIGH);
#endif
}

void relay1On(void) {
    relay1 = true;
    digitalWrite(2, LOW);
}

void relay1Off(void) {
    relay1 = false;
    digitalWrite(2, HIGH);
}

// setup the output serial port (used for debugging)
// connect to the wifi AP
// setup and start the mDNS responder with hostname ESP_XXYYZZ 
//      where XXYYZZ are the lowercase upper bytes of the MAC address
// setup and start the webserver
void setup(void)
{    
    Serial.begin(115200);

    // initialize pins & relays
    // relay 0
#ifndef IGNORE_GPIO0
    pinMode(0, OUTPUT);
#endif
    relay0Off();
    // relay 1
    pinMode(2, OUTPUT);
    relay1Off();

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
    Serial.println(WiFi.localIP());
    // get mac address and assemble hostname for mDNS
    WiFi.macAddress(mac);
	Serial.print("MAC: ");
	Serial.print(mac[5],HEX);
	Serial.print(":");
	Serial.print(mac[4],HEX);
	Serial.print(":");
	Serial.print(mac[3],HEX);
	Serial.print(":");
	Serial.print(mac[2],HEX);
	Serial.print(":");
	Serial.print(mac[1],HEX);
	Serial.print(":");
	Serial.println(mac[0],HEX);
    snprintf(mDNSName, MDNS_NAME_MAX_LENGTH,
            "ESP_%X%X%X", mac[5], mac[4], mac[3]
        );
    // register hostname w/ mDNS 
    Serial.println("");
    if (!mdns.begin(mDNSName, WiFi.localIP())) {
        Serial.println("Error setting up MDNS responder!");
        while(1) { 
            delay(500);
            Serial.print(".");
        }
    }
    Serial.print("mDNS responder started with hostname: ");
    Serial.println(mDNSName);

    // setup web server
    server.on("/", handleRoot);
    server.on("/relay/0/on", handleRelay0On);
    server.on("/relay/0/off", handleRelay0Off);
    server.on("/relay/1/on", handleRelay1On);
    server.on("/relay/1/off", handleRelay1Off);
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
		mDNSName,
        hr, min % 60, sec % 60,
        (relay0 ? "<b>" : ""), (relay0 ? "</b>" : ""), (relay0 ? "" : "<b>"), (relay0 ? "" : "</b>"),
        (relay1 ? "<b>" : ""), (relay1 ? "</b>" : ""), (relay1 ? "" : "<b>"), (relay1 ? "" : "</b>")
	);
	server.send(768, "text/html", temp);
}

void handleRelay0On(void) {
    Serial.println("turning on relay 0");
    relay0On();
    sendIndexPage();
}

void handleRelay0Off(void) {
    Serial.println("turning off relay 0");
    relay0Off();
    sendIndexPage();
}

void handleRelay1On(void) {
    Serial.println("turning on relay 1");
    relay1On();
    sendIndexPage();
}

void handleRelay1Off(void) {
    Serial.println("turning off relay 1");
    relay1Off();
    sendIndexPage();
}

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

