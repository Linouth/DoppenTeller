#include <Arduino.h>
#include <Time.h>
#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include "config.h"

enum messages {
    MSG_GET_COUNT = 0x1,
};

#define PIN_PHOTODIODE A0
#define PIN_IRLED D8
#define PIN_STATUS D4

#define MAX_UDP_INTERVAL 10
#define CAP_LIST_SIZE 30

typedef struct __attribute__((__packed__)) time_m_ {
    time_t time;
    uint16_t millis;
} time_m_t;

typedef struct {
    uint32_t count;
    time_m_t times[CAP_LIST_SIZE];
} capdata_t;

capdata_t caps = { 0 };
WiFiUDP Udp;

bool is_synced_once = false;

void error(uint wait) {
    bool led = LOW;
    Serial.println("ERROR");

    while (true) {
        digitalWrite(PIN_STATUS, led);
        led = !led;
        delay(wait);
    }
}

void add_cap(time_t time, uint32_t millis) {
    for (int i = 0; i < (CAP_LIST_SIZE-1); i++) {
        caps.times[i+1] = caps.times[i];
    }
    caps.times[0].time = time;
    caps.times[0].millis = millis;
    caps.count++;
}

uint32_t get_remote_caps() {
    char buf[4];
    int len;

    Udp.beginPacket(serverAddr, serverPort);
    Udp.write(MSG_GET_COUNT);
    Udp.endPacket();

    delay(20);

    len = Udp.parsePacket();
    len = Udp.read(buf, 4);

    if (!len)
        return 0;
    return buf[0] | buf[1] << 8 | buf[2] << 16 | buf[3] << 24;
}

bool update_server() {
    uint32_t remote_caps = get_remote_caps();
    uint32_t missing;

    if (remote_caps == 0) {
        Serial.println("Server is unreachable");
        return false;
    }

    if (remote_caps < caps.count) {
        // Server is not up-to-date
        missing = caps.count - remote_caps;
        Serial.printf("Server is missing %d caps, updating... ", missing);

        if (missing > CAP_LIST_SIZE) {
            Serial.printf("\nWarning! server is missing %d caps, only sending the last %d caps... ",
                    missing, CAP_LIST_SIZE);
            missing = CAP_LIST_SIZE;
        }

        // Server sends times from newest to oldest
        Udp.beginPacket(serverAddr, serverPort);
        Udp.write((uint8_t*)&caps.count, 4);
        Udp.write((uint8_t*)&caps.times, 6*missing);
        Udp.endPacket();

        Serial.println("Done");
    } else if (remote_caps > caps.count) {
        Serial.printf("Something has gone wrong! remote_caps: %d, local_caps: %d\n",
                remote_caps, caps.count);
        error(1000);
        return false;  // For clarity, never going to reach this
    } else {
        Serial.println("Server is up-to-date");
    }

    return true;
}

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
        if (!is_synced_once)
            error(500);
    }
    payload = http.getString();
    // Serial.println(payload);
    t = parse_time(payload);
    setTime(t + 3600*2);  // UTC+2 hacky fix
    is_synced_once = true;
}

void store_capdata() {
    uint8_t *p = (uint8_t*)&caps;
    
    for (uint i = 0; i < sizeof(capdata_t); i++) {
        EEPROM.write(i, p[i]);
    }
    EEPROM.commit();
}

void load_capdata() {
    uint8_t *p = (uint8_t*)&caps;

    for (uint i = 0; i < sizeof(capdata_t); i++) {
        p[i] = EEPROM.read(i);
    }
}

bool save_and_sync() {
#ifndef DISABLE_CAP_UPDATE
    store_capdata();
    return update_server();
#else
    return true;
#endif
}

bool caps_synced;
void setup() {
    Serial.begin(115200);
    Serial.println("Serial enabled");

    pinMode(PIN_IRLED, OUTPUT);
    pinMode(PIN_STATUS, OUTPUT);
    digitalWrite(PIN_STATUS, HIGH);

    WiFi.begin(SSID, PWD);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("WiFi Failed!");
        return;
    }
    Serial.println("WiFi Connected!");
    Serial.println(WiFi.localIP());

    ArduinoOTA.onStart([]() {
        Serial.println("Force save and sync cap data");
        save_and_sync();
        Serial.println("Start OTA Update");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("OTA End");
        Serial.println("Rebooting...");
    });
    ArduinoOTA.setPassword(OTAPASS);
    ArduinoOTA.begin();

    Udp.begin(serverPort);
    Udp.setTimeout(250);

    sync_clock();

    EEPROM.begin(256);

    // caps.count = 106376;
    // store_capdata();
    load_capdata();

    Serial.printf("Caps: %d, time[0] = %ld, time[1] = %ld, time[2] = %ld ...\n",
            caps.count, caps.times[0].time, caps.times[1].time, caps.times[2].time);

    caps_synced = update_server();
}

uint val;
uint seconds;
bool cap_detected = false;
bool caps_just_synced = false;
bool clock_synced = false;
bool checked_for_update = false;
void loop() {
    seconds = second();

    // Check for OTA update every 10 seconds
    if (!checked_for_update && seconds%10 == 0) {
        ArduinoOTA.handle();
        checked_for_update = true;
    } else if (seconds%10 != 0) {
        checked_for_update = false;
    }

    // Sync clock every morning
    if (hour() == 6){
        if (!clock_synced) {
            sync_clock();
            clock_synced = true;
        }
    } else {
        clock_synced = false;
    }

    // Dont really need to check these times...
    // TODO: Let esp go into deepsleep
    if (hour() > 4 && hour() < 9) {
        delay(60 * 1000);
        return;
    }

    // Do measurement
    digitalWrite(PIN_IRLED, HIGH);
    val = analogRead(PIN_PHOTODIODE);
    digitalWrite(PIN_IRLED, LOW);

    if (!cap_detected && val > 130) {
        // Peak in measurement, cap detected
        Serial.println("Cap detected");
        cap_detected = true;
        add_cap(now(), millis()%1000);

        digitalWrite(PIN_STATUS, LOW);
    } else if (cap_detected && val < 70) {
        // Peak back down
        cap_detected = false;
        caps_synced = false;

        digitalWrite(PIN_STATUS, HIGH);
    }

    // If new caps are available, try to sync them every minute
    if (!caps_just_synced && !caps_synced && seconds == 0) {
        Serial.println("Syncing");
        caps_synced = save_and_sync();
        caps_just_synced = true;
    }

    // Reset flags
    if (seconds != 0) {
        caps_just_synced = false;
        checked_for_update = false;
    }

#ifdef DEBUG
    Udp.beginPacket(serverAddr, serverPort);
    Udp.printf("%d", val);
    Udp.endPacket();
#endif

    delay(10);
}