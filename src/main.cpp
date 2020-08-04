#include <Arduino.h>
#include <Time.h>
#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

#include "config.h"

#define PIN_PHOTODIODE A0
#define PIN_IRLED D2

uint64_t capCount = 106358;

WiFiUDP Udp;

int parse_time(String str) {
    int start = str.indexOf("unixtime:");
    int end = str.indexOf("\n", start);
    String out = str.substring(start+10, end);
    return out.toInt();
}

void sync_clock() {
    WiFiClient client;
    HTTPClient http;
    int httpCode;
    String payload;
    time_t t;

    Serial.println("Synching clock");

    http.begin(client, "http://worldtimeapi.org/api/timezone/Europe/Amsterdam.txt");
    httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("Could not get current time from worldtimeapi.org: %d", httpCode);
        for(;;);
    }
    payload = http.getString();
    Serial.println(payload);
    t = parse_time(payload);
    Serial.println(t);
    Serial.println(numberOfSeconds(t));
    setTime(t + 3600*2);  // UTC+2 hacky fix
}

void store_count(uint64_t count) {
    uint8_t *c = (uint8_t *) &count;
    
    for (int i = 0; i < sizeof(count); i++) {
        EEPROM.write(i, c[i]);
    }
    EEPROM.commit();
}

uint64_t load_count() {
    uint8_t c[8];
    uint64_t count;

    for (int i = 0; i < sizeof(count); i++) {
        c[i] = EEPROM.read(i);
    }
    count = *((uint64_t*)&c);
    return count;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Serial enabled");

    WiFi.begin(SSID, PWD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("WiFi Failed!");
        return;
    }
    Serial.println("WiFi Connected!");
    Serial.println(WiFi.localIP());

    sync_clock();

    EEPROM.begin(8);
    capCount = load_count();

    pinMode(PIN_IRLED, OUTPUT);
}

int val;
bool synced = false;
bool capDetected = false;
void loop() {
    /* Sync clock every morning */
    if (hour() == 6){
        if (!synced) {
            sync_clock();
            synced = true;
        }
    } else {
        synced = false;
    }

    /* Not really need to check these times... */
    // TODO: Let esp go into deepsleep
    if (hour() > 4 && hour() < 9) {
        delay(60 * 1000);
        return;
    }

    digitalWrite(PIN_IRLED, HIGH);
    val = analogRead(PIN_PHOTODIODE);
    digitalWrite(PIN_IRLED, LOW);

    if (!capDetected && val < 650) {
        capDetected = true;
        capCount++;
    } else if (capDetected && val > 710) {
        capDetected = false;

        Serial.printf("Sending packet: %d\n", val);
        Udp.beginPacket(serverAddr, serverPort);
        Udp.printf("%d", capCount);
        Udp.endPacket();
    }

    delay(15);
}