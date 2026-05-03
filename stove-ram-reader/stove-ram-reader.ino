/*
Uses libraries:

'ESP Async WebServer' by ESP32Async
https://github.com/ESP32Async/ESPAsyncWebServer

'Async TCP' by ESP32Async
https://github.com/ESP32Async/AsyncTCP

Starts webserver at port 80.
Exposes IP via mDNS address 'esp32.local'

Use:
Main page: http://esp32.local/ 

    Http parameters:
    - http://esp32.local/?ram
    Only updates the ram registers

    - http://esp32.local/?ram=[0-255]
    Only updates a single ram register

    - http://esp32.local/?eeprom
    Only updates the eeprom registers

    - http://esp32.local/?eeprom=[0-255]
    Only updates a single eeprom register

Json data: http://esp32.local/dump   

*/
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include "micronova_stove.h"
#include "arduino_secrets.h"


// ===== WIFI CONFIG =====
const char* ssid = SECRET_wifi_ssid;
const char* password = SECRET_wifi_password;

// ===== PINS =====
#define RX_PIN 16
#define TX_PIN 26
#define RX_ENABLE_PIN 36

// ===== OBJECTS =====
MicronovaStove stove(RX_PIN, TX_PIN, RX_ENABLE_PIN);
AsyncWebServer server(80);

// ===== CACHE =====
String cachedJson = "{}";
unsigned long lastUpdate = 0;
const int updateInterval = 1000; // ms

unsigned long lastUpdate2 = 0;
const int updateInterval2 = 1000; // ms

uint8_t ram[256];
uint8_t eeprom[256];

uint16_t ram_16[256/2];
uint16_t eeprom_16[256/2];

uint16_t ram_32[256/4];
uint16_t eeprom_32[256/4];

char buf[8000];

int read_ram_register = -1;
int read_eeprom_register = -1;

// ===== HTML PAGE =====
const char htmlPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Micronova Monitor</title>
  <style>
    body { font-family: monospace; background:#111; color:#0f0; }
    pre { font-size: 12px; }
  </style>
</head>
<body>
<h1>Micronova Register Dump</h1>
<pre id="out">Loading...</pre>

<script>
async function load() {
  try {
    let res = await fetch('/dump');
    let data = await res.json();
    document.getElementById('out').textContent =
      JSON.stringify(data, null, 2);
  } catch(e) {
    console.error(e);
    document.getElementById('out').textContent = "Error loading data";
  }
}
setInterval(load, 1000);
load();
</script>

</body>
</html>
)rawliteral";

// ===== READ FUNCTIONS =====
uint8_t readRam(uint8_t addr) {
    stove.read_ram(addr);
    delay(10); // critical for stability
    return stove.last_read_value;
}

uint8_t readEeprom(uint8_t addr) {
    stove.read_eeprom(addr);
    delay(10);
    return stove.last_read_value;
}

// ===== DUMP FUNCTION =====

void readAll() {

    // Read
    Serial.print("ram..");
    if (read_ram_register == -1) { 
        if (read_eeprom_register == -1) {  // only of no special register is requested from both RAM or EEPROM
            for (int i = 0; i < sizeof(ram); i++) {
                ram[i] = readRam(i);
            }
        }
    } else {
        ram[read_ram_register] = readRam(read_ram_register);
    }

    Serial.print("eeprom..");
    if (read_eeprom_register == -1) { 
        if (read_ram_register == -1) {  // only of no special register is requested from both RAM or EEPROM
            for (int i = 0; i < sizeof(eeprom); i++) {
                eeprom[i] = readEeprom(i);
            }
        }
    } else {
        eeprom[read_eeprom_register] = readEeprom(read_eeprom_register);
    }

    // Copy
    for (int i = 0; i < sizeof(ram); i+=2) {
        ram_16[i/2] = ((uint16_t)ram[i]<<16) | ram[i+1];
    }
    for (int i = 0; i < sizeof(eeprom); i+=2) {
        eeprom_16[i/2] = ((uint16_t)eeprom[i]<<16) | eeprom[i+1];
    }

    // Copy
    for (int i = 0; i < sizeof(ram_16); i+=2) {
        ram_32[i/4] = ((uint32_t)ram_16[i]<<32) | ram_16[i+1];
    }
    for (int i = 0; i < sizeof(eeprom_16); i+=2) {
        eeprom_32[i/4] = ((uint16_t)eeprom_16[i]<<32) | eeprom_16[i+1];
    }

}


void dumpRegistersOneLine(char* out) {

    out += sprintf(out, "{\n");
    for (size_t i = 0; i < sizeof(ram)-1; i += 16) {

        out += sprintf(out,
            "\"RAM%03u\":\""
            "%3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u | "
            "%5u %5u %5u %5u %5u %5u %5u %5u | "
            "%10u %10u %10u %10u\",\n",
            i,
            ram[i+0], ram[i+1], ram[i+2], ram[i+3],
            ram[i+4], ram[i+5], ram[i+6], ram[i+7],
            ram[i+8], ram[i+9], ram[i+10], ram[i+11],
            ram[i+12], ram[i+13], ram[i+14], ram[i+15],

            ram_16[i/2 + 0], ram_16[i/2 + 1], ram_16[i/2 + 2], ram_16[i/2 + 3],
            ram_16[i/2 + 4], ram_16[i/2 + 5], ram_16[i/2 + 6], ram_16[i/2 + 7],

            ram_32[i/4 + 0], ram_32[i/4 + 1], ram_32[i/4 + 2], ram_32[i/4 + 3]
        );
    }

    for (size_t i = 0; i < sizeof(ram)-1; i += 16) {

        out += sprintf(out,
            "\"EEPROM%03u\":\""
            "%3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u %3u | "
            "%5u %5u %5u %5u %5u %5u %5u %5u | "
            "%10u %10u %10u %10u\",\n",
            i,
            eeprom[i+0], eeprom[i+1], eeprom[i+2], eeprom[i+3],
            eeprom[i+4], eeprom[i+5], eeprom[i+6], eeprom[i+7],
            eeprom[i+8], eeprom[i+9], eeprom[i+10], eeprom[i+11],
            eeprom[i+12], eeprom[i+13], eeprom[i+14], eeprom[i+15],

            eeprom_16[i/2 + 0], eeprom_16[i/2 + 1], eeprom_16[i/2 + 2], eeprom_16[i/2 + 3],
            eeprom_16[i/2 + 4], eeprom_16[i/2 + 5], eeprom_16[i/2 + 6], eeprom_16[i/2 + 7],

            eeprom_32[i/4 + 0], eeprom_32[i/4 + 1], eeprom_32[i/4 + 2], eeprom_32[i/4 + 3]
        );
    }

    out += sprintf(out, "\"ram\":\"%d\",", read_ram_register);
    out += sprintf(out, "\"eeprom\":\"%d\",", read_eeprom_register);

    out += sprintf(out, "\"time\":\"%d\"\n}", lastUpdate2/1000);

    *out = '\0';   // terminate string
}


// ===== WIFI =====
void setupWiFi() {
    WiFi.begin(ssid, password);
    Serial.print("Connecting");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nConnected!");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("esp32")) {
        Serial.println("Error starting mDNS");
    } else {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS started: http://esp32.local");
    }

}

// ===== WEB SERVER =====
void setupWeb() {

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){

        if (request->hasParam("ram")) {
            String value = request->getParam("ram")->value();
            read_ram_register = value.toInt();
            read_ram_register &= 0xFF;
        } else {
            read_ram_register = -1;
        }

        if (request->hasParam("eeprom")) {
            String value = request->getParam("eeprom")->value();
            read_eeprom_register = value.toInt();
            read_eeprom_register &= 0xFF;
        } else {
            read_eeprom_register = -1;
        }

        request->send_P(200, "text/html", htmlPage);
    });

    server.on("/dump", HTTP_GET, [](AsyncWebServerRequest *request){

        if (millis() - lastUpdate2 > updateInterval2) {
            dumpRegistersOneLine(buf);
            cachedJson = String(buf);

            lastUpdate2 = millis();            
        }

        request->send(200, "application/json", cachedJson);
    });

    server.begin();
}

// ===== SETUP =====
void setup() {
    Serial.begin(115200);

    pinMode(RX_PIN, INPUT);
    pinMode(TX_PIN, OUTPUT);
    pinMode(RX_ENABLE_PIN, OUTPUT);
    stove.init();  // init micronova serial

    setupWiFi();
    setupWeb();
}

void replace_newline(String& inputString) {
    // Or replace specific newlines
    for (int i = 0; i < inputString.length()-1; i++) {
        if (inputString[i] == '\\' && inputString[i+1] == 'n') {
            inputString[i] == ' ';
            inputString[i+1] = '\n'; // Replace newline with a space
        }
    }
}

// ===== LOOP =====
void loop() {

    // refresh cache every X seconds
    if (millis() - lastUpdate > updateInterval) {
        Serial.println("Updating register dump...");
        readAll();
        Serial.println("..done");

        lastUpdate = millis();
    }
}
