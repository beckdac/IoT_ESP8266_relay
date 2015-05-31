#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

/* wificred.h contains something like:
   const char* ssid = ".........";
   const char* password = ".........";
*/
#include "wificred.h"

// MAC address of this unit
byte mac[6];
String mDNSName;
const int mDNSNameCharLength = 12;
char mDNSNameChar[mDNSNameCharLength];

// multicast DNS responder
MDNSResponder mdns;

// TCP server at port 80 will respond to HTTP requests
WiFiServer server(80);

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
    mDNSName = String("ESP_") + String(mac[5], HEX) + String(mac[4], HEX) + String(mac[3], HEX);
    mDNSName.toCharArray(mDNSNameChar, mDNSNameCharLength);
   
    Serial.println("") 
    if (!mdns.begin(mDNSNameChar, WiFi.localIP())) {
        Serial.println("Error setting up MDNS responder!");
        while(1) { 
            delay(500);
            Serial.print(".");
        }
    }
    Serial.println("mDNS responder started with hostname " + mDNSName);
    
    // Start TCP (HTTP) server
    server.begin();
    Serial.println("TCP server started");
}

void loop(void)
{
    // Check if a client has connected
    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    Serial.println("");
    Serial.println("New client");

    // Wait for data from client to become available
    while(client.connected() && !client.available()){
        delay(1);
    }
    
    // Read the first line of HTTP request
    String req = client.readStringUntil('\r');
    
    // First line of HTTP request looks like "GET /path HTTP/1.1"
    // Retrieve the "/path" part by finding the spaces
    int addr_start = req.indexOf(' ');
    int addr_end = req.indexOf(' ', addr_start + 1);
    if (addr_start == -1 || addr_end == -1) {
        Serial.print("Invalid request: ");
        Serial.println(req);
        return;
    }
    req = req.substring(addr_start + 1, addr_end);
    Serial.print("Request: ");
    Serial.println(req);
    client.flush();
    
    String s;
    if (req == "/")
    {
        IPAddress ip = WiFi.localIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 at ";
        s += ipStr;
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
    }
    else
    {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
    }
    client.print(s);
    
    Serial.println("Done with client");
}
