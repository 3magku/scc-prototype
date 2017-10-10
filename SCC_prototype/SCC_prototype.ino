/*
   ESP8266 / TMP006 / HTTP server / myDevices Cayenne MQTT client

   Board:   LoLin NodeMCU v3 (ESP8266)
            NodeMCU 1.0 (ESP-12E Module), 80 MHz, 115200, 4M (§M SPIFFS)
   Sensor:  TI TMP006 Infrared Thermopile Contactless Temperature Sensor (Adafruit breakout)
   Wiring:
    TMP006    GND   ->  GND NodeMCU
              SCL   ->  D1
              SDA   ->  D2
              VIN   ->  3,3V
*/

/* *** imports *************************************************************** */

// TMP006:
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include "Adafruit_TMP006.h"
Adafruit_TMP006 tmp006;

// WiFi client & web server:
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// Cayenne:
#define CAYENNE_DEBUG
#define CAYENNE_PRINT Serial
#include <CayenneMQTTESP8266.h>

// SPIFFS
#include "FS.h"

/* *** declarations ********************************************************* */

// TMP006 sensor readings;
float tmp006DieTempC, tmp006ObjectTempC;

// RGB LED
int rgbLedRedPin = 12;
int rgbLedgreenPin = 13;
int rgbLedBluePin = 15;

// Web server:
const char *httpServiceName = "scc";
const int httpServicePort = 80;
ESP8266WebServer httpServer(80);

/* ************************************************************************** */
/* *** WiFi & Cayenne configuration ***************************************** */
/* ************************************************************************** */
// WiFi network info:
const char* ssid = "00000000";
const char* wifiPassword = "00000000";

// Cayenne authentication info:
char username[] = "00000000-0000-0000-0000-000000000000";
char password[] = "00000000-0000-0000-0000-000000000000";
char clientID[] = "00000000-0000-0000-0000-000000000000";
/* ************************************************************************** */

// SCC logic
const int sccDieTmpObjTmpTolerance = 2;
int sccThresholdTemperature = -1;
float sccThresholdDifference = 0;
bool sccCupStatusChanged = false;

// miscellaneous
const int loopDelay = 2000;

// SPIFFS
bool useLocalStorage = false;

/* *** functions ************************************************************ */

// Sensor;
void readSensor() {
  // Read sensor:
  tmp006DieTempC = tmp006.readDieTempC();
  tmp006ObjectTempC = tmp006.readObjTempC();
  Serial.println("Die temperature *C\tObject temperature *C");
  Serial.print(tmp006DieTempC); Serial.print("\t"); Serial.println(tmp006ObjectTempC);
}

// RGB LED: setColor
void setColor(int red, int green, int blue)
{
#ifdef COMMON_ANODE
  red = 255 - red;
  green = 255 - green;
  blue = 255 - blue;
#endif
  analogWrite(rgbLedRedPin, red);
  analogWrite(rgbLedgreenPin, green);
  analogWrite(rgbLedBluePin, blue);
}

// Web server: handler
void handleRoot() {
  httpServer.send(200, "text/plain", "die temperature (*C) = "  + String(tmp006DieTempC) + " / object temperature (*C) = "  + String(tmp006ObjectTempC));
}

void handleJson() {
  httpServer.send(200, "application/json", "{\"temperature\": {\"die\":" + String(tmp006DieTempC) + ",\"object\":" + String(tmp006ObjectTempC) + "}}");
}

void handleNotFound() {
  setColor(255, 255, 255);
  String message = "Resource Not Found\n\n";
  message += "URI: ";
  message += httpServer.uri();
  message += "\nMethod: ";
  message += (httpServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += httpServer.args();
  message += "\n";
  for (uint8_t i = 0; i < httpServer.args(); i++) {
    message += " " + httpServer.argName(i) + ": " + httpServer.arg(i) + "\n";
  }
  httpServer.send(404, "text/plain", message);
}

// Cayenne:
// Default function for processing actuator commands from the Cayenne Dashboard.
// Since the activation of the functions for specific channels, 
// e.g. CAYENNE_IN(4) for channel 4 commands did not seem to work, 
// we use the default function and a switch statement.
CAYENNE_IN_DEFAULT()
{
  CAYENNE_LOG("CAYENNE_IN_DEFAULT(%u) - %s, %s", request.channel, getValue.getId(), getValue.asString());
  //Process message here. If there is an error set an error message using getValue.setError(), e.g getValue.setError("Error message");
  switch (request.channel) {
    case 4:
      sccThresholdTemperature = getValue.asInt();
      Serial.print("Threshold temperature changed to ");
      Serial.print(sccThresholdTemperature);
      Serial.println(" *C");
      if ( useLocalStorage ) {
        // open file for writing
        File f = SPIFFS.open("/setup", "w");
        if (!f) {
          Serial.println("SPIFFS open failed");
        } else {
          Serial.println("Writing to SPIFFS file ...");
          f.println(sccThresholdTemperature);
          f.close();
        }
      }
      break;
  }
}

// SCC logic
bool sccNoCup() {
  return ( tmp006ObjectTempC - tmp006DieTempC < sccDieTmpObjTmpTolerance || tmp006DieTempC > tmp006ObjectTempC ) ;
}

/* *** setup **************************************************************** */

void setup(void) {
  Serial.begin(115200);

  // Welcome ...
  Serial.println("NodeMCU v3 (ESP8266) with TMP006 infrared thermometer, RGB LED, HTTP server, myDevices Cayenne MQTT cient");
  Serial.println("Initializing ...");

  // RGB LED:
  pinMode(rgbLedRedPin, OUTPUT);
  pinMode(rgbLedgreenPin, OUTPUT);
  pinMode(rgbLedBluePin, OUTPUT);
  setColor(128, 0, 0); // "low red" while initializing ...

  // TMP006 sensor:
  if (!tmp006.begin(TMP006_CFG_16SAMPLE)) { // 16 samples / 4 seconds per reading
    Serial.println("No TMP006 sensor found");
    while (1);
  }
  Serial.println("TMP006 sensor initialized.");

  // WiFi:
  Serial.println("Connecting ...");
  // WiFi client setup is handled in Cayenne.begin() ...
  /*
    WiFi.begin(wifiSsid, wifiPassword);
    // Wait for connection
    while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(wifiSsid);
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  */

  // Cayenne:
  Cayenne.begin(username, password, clientID, wifiSsid, wifiPassword);
  Serial.println("Connected.");

  // Web server:
  if (MDNS.begin(httpServiceName)) {
    MDNS.addService("http", "tcp", httpServicePort);
    Serial.print("MDNS responder started for name '");
    Serial.print(httpServiceName);
    Serial.println("'.");
  }
  httpServer.on("/", handleRoot);
  httpServer.on("/json", handleJson);
  httpServer.on("/about", []() {
    httpServer.send(200, "text/plain", "NodeMCU v3 (ESP8266) with TMP006 infrared thermometer, RGB LED, HTTP server, myDevices Cayenne MQTT cient");
  });
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("HTTP server started.");

  // Display "ready" status with RGB LED:
  setColor(255, 255, 255); // bright white
  delay(loopDelay);
  setColor(0, 0, 0); // off

  // SPIFFS:
  if ( sccThresholdTemperature < 0 ) {
    useLocalStorage = true;
    SPIFFS.begin();
    
    /* SPIFFS format **********************************************************/
    /*
      // The following lines have to be excuted ONLY ONCE!
      // After SPIFFS is formatted ONCE these lines can be commented out.
      Serial.println("Please wait 30 secs for SPIFFS to be formatted ...");
      SPIFFS.format();
      Serial.println("SPIFFS formatted.");
      File f0 = SPIFFS.open("/setup", "w");
      if (!f0) {
      Serial.println("SPIFFS open failed");
      } else {
      Serial.println("Writing to SPIFFS file ...");
      f0.println(0);
      f0.close();
      }
      /* SPIFFS format ********************************************************/
      
    // open file for reading
    File f = SPIFFS.open("/setup", "r");
    if (!f) {
      Serial.println("SPIFFS open failed!");
    } else {
      Serial.println("Reading from SPIFFS file ...");
      String s = f.readStringUntil('\n');
      sccThresholdTemperature = s.toInt();
      f.close();
    }
    Cayenne.celsiusWrite(4, sccThresholdTemperature);
  }
}

/* *** loop ***************************************************************** */

void loop() {
  // Read sensor ...
  readSensor();

  // SCC logic
  if ( sccNoCup() ) {
    setColor(0, 255, 0); // green
    if ( !sccCupStatusChanged )  {
      sccCupStatusChanged = true;
      sccThresholdDifference = 0;
    }
  } else {
    if ( sccCupStatusChanged ) {
      // Wait another cycle ..
      delay(loopDelay);
      // And read sensor again ...
      readSensor();
      sccCupStatusChanged = false;
    }
    sccThresholdDifference =  tmp006ObjectTempC - sccThresholdTemperature;
    if ( sccThresholdDifference < 0 ) {
      setColor(0, 0, 255); // blue
    } else {
      setColor(255, 0, 0); // red
    }
  }

  // Cayenne:
  Cayenne.loop();
  Cayenne.virtualWrite(0, millis()); 
  // ^Channel 0: Uptime in ms
  Cayenne.celsiusWrite(1, tmp006DieTempC); 
  // ^Channel 1: TMP006 sensor reading: die temperature in *C
  Cayenne.celsiusWrite(2, tmp006ObjectTempC); 
  // ^Channel 2: TMP006 sensor reading: object temperature in *C
  Cayenne.celsiusWrite(3, sccThresholdDifference); 
  // ^Channel 3: difference between object (cup) and threshold temperature

  // Web server:
  httpServer.handleClient();

  delay(loopDelay);
}