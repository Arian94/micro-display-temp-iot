#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <Wire.h>

#define ssid "MERCUSYS_6A8EBB"
#define password ""
#define weatherAddr "https://api.open-meteo.com/v1/forecast?latitude=35.69&longitude=51.42&hourly=temperature_2m&daily=temperature_2m_max,temperature_2m_min&timezone=auto"
#define tempSensor PIN_A0
// ST7789 display module connections
#define display_DC D1   // display DC  pin is connected to NodeMCU pin D1 (GPIO5)
#define display_RST D2  // display RST pin is connected to NodeMCU pin D2 (GPIO4)
#define display_CS D8   // display CS  pin is connected to NodeMCU pin D8 (GPIO15)
// SDA D7 - SCK D5
Adafruit_ST7789 display = Adafruit_ST7789(display_CS, display_DC, display_RST);

void clearDisplay() {
    display.fillScreen(ST7735_ORANGE);
}

// Connect to WiFi AP
void connectToWiFi() {
    Serial.println();
    Serial.println("Connecting to WiFi...");

    clearDisplay();
    display.setCursor(10, 10);
    display.print("Connecting to WiFi");

    WiFi.begin(ssid, password);

    int retries = 0;
    while ((WiFi.status() != WL_CONNECTED) && (retries < 15)) {
        retries++;
        delay(500);
        Serial.print(".");
    }

    if (retries > 14) {
        Serial.println(F("WiFi connection FAILED"));
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("WiFi connected!"));
        Serial.printf("IP address: ");
        Serial.println(WiFi.localIP());
    }
}

float_t getAverage(float_t array[], size_t array_size) {
    float_t sum = 0;
    for (size_t i = 0; i < array_size; i++)
        sum += array[i];

    return sum / array_size;
}

void displayFailureWithDelay(const char *message, size_t retry_delay = 5) {
    clearDisplay();
    display.printf(message);
    delay(2000);
    clearDisplay();

    for (size_t i = retry_delay; i--;) {
        Serial.printf("retrying in %ds\n", i);
        display.setCursor(10, 10);
        display.printf("retrying in %ds", i);
        delay(1000);
        clearDisplay();
    }
}

void getWeather() {
    clearDisplay();
    display.setCursor(10, 10);
    display.print("Getting weather");

    if ((WiFi.status() == WL_CONNECTED)) {
        Serial.println("getting weather...");
        std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);

        // Or, if you happy to ignore the SSL certificate, then use the following line instead:
        client->setInsecure();

        HTTPClient https;

        Serial.print("[HTTPS] begin...\n");
        if (https.begin(*client, weatherAddr)) {  // HTTPS

            Serial.print("[HTTPS] GET...\n");
            // start connection and send HTTP header
            int httpCode = https.GET();

            // httpCode will be negative on error
            if (httpCode > 0) {
                // HTTP header has been send and Server response header has been handled
                Serial.printf("[HTTPS] GET... code: %d\n", httpCode);

                // file found at server
                if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
                    String payload = https.getString();

                    // Allocate a temporary JsonDocument
                    // Don't forget to change the capacity to match your requirements.
                    // Use https://arduinojson.org/v6/assistant to compute the capacity.
                    const size_t filter_cap = JSON_ARRAY_SIZE(168) + 2 * JSON_OBJECT_SIZE(1) + 64;
                    DynamicJsonDocument payloadAsObject(filter_cap);

                    DynamicJsonDocument filter(filter_cap);
                    filter["hourly"]["temperature_2m"] = true;

                    // Deserialize the JSON document
                    DeserializationError error = deserializeJson(payloadAsObject, payload.c_str(), DeserializationOption::Filter(filter));
                    if (error) Serial.printf("Failed to deserialize: %s\n", error.c_str());

                    payload.clear();
                    filter.clear();

                    char averages_str[35] = {0};
                    float_t dailyTemps[24];
                    size_t size = payloadAsObject["hourly"]["temperature_2m"].size();

                    for (size_t i = 0; i < size; i++) {
                        size_t remainder = (i + 1) % 24;
                        if (remainder == 0) {
                            dailyTemps[23] = payloadAsObject["hourly"]["temperature_2m"][i];
                            char avg[5];
                            sprintf(avg, "%3.1lf\t", getAverage(dailyTemps, *(&dailyTemps + 1) - dailyTemps));
                            strcat(averages_str, avg);
                        } else {
                            dailyTemps[remainder - 1] = payloadAsObject["hourly"]["temperature_2m"][i];
                        }
                    }
                    payloadAsObject.clear();
                    Serial.println(averages_str);
                    int16_t rawVal = analogRead(tempSensor);
                    int16_t kelvin = rawVal * .298;
                    int8_t tempC = kelvin - 273;

                    clearDisplay();
                    display.setCursor(10, 10);
                    display.setTextColor(ST7735_GREEN);
                    display.printf("Room temp: %d\tC\n\n", tempC);
                    // display.setCursor(10, 30);
                    display.setTextColor(ST7735_WHITE);
                    display.println(averages_str);

                    Serial.println("Wait 5min before next round...");
                } else {
                    char error[50];
                    sprintf(error, "request failed. HTTP Code: %d", httpCode);
                    Serial.println(error);
                    displayFailureWithDelay(error, 5);
                }
            } else {
                const char *error = https.errorToString(httpCode).c_str();
                Serial.printf("[HTTPS] GET... failed, error: %s\n", error);
                displayFailureWithDelay(error, 5);
            }

            https.end();
        } else {
            Serial.printf("[HTTPS] Unable to connect\n");
            displayFailureWithDelay("Unable to connect", 5);
            connectToWiFi();
        }
    } else {
        Serial.printf("WiFi disconnected\n");
        displayFailureWithDelay("WiFi disconnected", 5);
        connectToWiFi();
    }
}

struct TouchSensor {
    const uint8_t PIN;
    bool pressed;
};

TouchSensor touchSensor = {D6, false};
// variables to keep track of the timing of recent interrupts
unsigned long buttonTime = 0;
unsigned long lastButtonTime = 0;
bool hasFirstReqSent = false;
unsigned long reqTime = 0;
unsigned long lastReqTime = 0;
bool refreshWeather = false;

void IRAM_ATTR touchSensorIsr() {
    buttonTime = millis();
    if (buttonTime - lastButtonTime > 500) {
        touchSensor.pressed = !touchSensor.pressed;
        lastButtonTime = buttonTime;
        Serial.println("Touch sensor pressed.");
        // display.ssd1306_command(SSD1306_SETCONTRAST);
        // display.ssd1306_command(touchSensor.pressed ? 2 : 255);
        refreshWeather = true;
    }
}

void setup() {
    Serial.begin(9600);

    display.init(240, 240, SPI_MODE2);
    clearDisplay();
    display.setRotation(2);

    while (!Serial)
        ;
    delay(1000);
    Serial.println("temp-iot");

    display.setTextSize(3);
    display.setCursor(50, 100);
    display.print("Welcome");
    delay(2000);
    display.setTextSize(2);

    connectToWiFi();
    pinMode(touchSensor.PIN, INPUT_PULLUP);
    attachInterrupt(touchSensor.PIN, touchSensorIsr, FALLING);
}

void loop() {
    if (!hasFirstReqSent) {
        getWeather();
        hasFirstReqSent = true;
        return;
    }

    reqTime = millis();
    unsigned long time_passed = reqTime - lastReqTime;
    if (time_passed > 300000) {
        getWeather();
        return;
    }

    if (refreshWeather) {
        refreshWeather = false;
        getWeather();
    }

    delay(1000);
}