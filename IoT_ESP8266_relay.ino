#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/* wificred.h contains something like:
   const char* ssid = ".........";
   const char* password = ".........";
*/
#include "wificred.h"

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

// setup the output serial port (used for debugging)
// connect to the wifi AP
// setup and start the mDNS responder with hostname ESP_XXYYZZ 
//      where XXYYZZ are the lowercase upper bytes of the MAC address
// setup and start the webserver
void setup(void)
{    
    Serial.begin(115200);

    // Connect to WiFi network
    WiFi.begin(ssid, password);
    Serial.println("");    
    
    // Wait for connection
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
    server.on("/relay/1/on", handleRelay0On);
    server.on("/relay/1/off", handleRelay0Off);
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

	snprintf ( temp, 400,

"<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>%s</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <center>\
        <p>Uptime: %02d:%02d:%02d</p>\
        <hr>\
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
	server.send(200, "text/html", temp);
}

void handleRelay0On(void) {
    relay0 = true;
    Serial.println("turning on relay 0");
    sendIndexPage();
}

void handleRelay0Off(void) {
    relay0 = false;
    Serial.println("turning off relay 0");
    sendIndexPage();
}

void handleRelay1On(void) {
    relay1 = true;
    Serial.println("turning on relay 1");
    sendIndexPage();
}

void handleRelay1Off(void) {
    relay1 = false;
    Serial.println("turning off relay 1");
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

