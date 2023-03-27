#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <Wire.h>

const char *ssid = "MERCUSYS_6A8EBB";
const char *password = "my name is_aDam";
const char *weatherAddr = "https://api.open-meteo.com/v1/forecast?latitude=35.69&longitude=51.42&hourly=temperature_2m&daily=temperature_2m_max,temperature_2m_min&timezone=auto";

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels

#define OLED_RESET -1        // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Connect to WiFi AP
void connectToWiFi() {
    Serial.println();
    Serial.println("Connecting to WiFi...");

    display.clearDisplay();
    display.setCursor(0, 1);
    display.println("Connecting to WiFi");
    display.display();

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
        Serial.println("IP address: ");
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
    display.clearDisplay();
    display.printf(message);
    display.display();
    delay(2000);
    display.clearDisplay();

    for (size_t i = retry_delay; i--;) {
        Serial.printf("retrying in %ds\n", i);
        display.setCursor(0, 2);
        display.printf("retrying in %ds", i);
        display.display();
        delay(1000);
        display.clearDisplay();
    }
}

void getWeather() {
    display.clearDisplay();
    display.setCursor(0, 1);
    display.println("Getting weather");
    display.display();

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
                    display.clearDisplay();
                    display.setCursor(0, 0);
                    display.println(averages_str);
                    display.display();

                    Serial.println("Wait 5min before next round...");
                    delay(300000);
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

TouchSensor touch_sensor = {D7, false};

// variables to keep track of the timing of recent interrupts
unsigned long button_time = 0;
unsigned long last_button_time = 0;
void IRAM_ATTR touch_sensor_isr() {
    button_time = millis();
    if (button_time - last_button_time > 500) {
        touch_sensor.pressed = !touch_sensor.pressed;
        last_button_time = button_time;
        Serial.println("Touch sensor pressed.");
        display.ssd1306_command(SSD1306_SETCONTRAST);
        display.ssd1306_command(touch_sensor.pressed ? 2 : 255);
    }
}

void setup() {
    Serial.begin(9600);
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;  // Don't proceed, loop forever
    }

    while (!Serial)
        ;
    delay(2000);
    Serial.println("temp-iot");
    delay(2000);
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 1);
    // Display static text
    display.println("Welcome");
    // Show initial display buffer contents on the screen --
    // the library initializes this with an Adafruit splash screen.
    display.display();
    delay(2000);  // Pause for 2 seconds

    connectToWiFi();
    pinMode(touch_sensor.PIN, INPUT_PULLUP);
    attachInterrupt(touch_sensor.PIN, touch_sensor_isr, FALLING);
}

void loop() {
    getWeather();
}