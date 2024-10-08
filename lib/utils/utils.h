#ifndef UTILS_H
#define UTILS_H


#include <sdkconfig.h>
#include <stdio.h>
#include <Arduino.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoOTA.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPDashPro.h>
#include "time.h"
#include "AsyncJson.h"
#include <ArduinoJson.h>

#include "config.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

// get ESP-IDF Certificate Bundle
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

// Preferences struct for storing and loading in nvs
struct PanelPrefs
{
    uint8_t brightness = 255;
    bool development = 0;
    bool ota = 0;
    bool github = 1;
    bool signedFWOnly = 1;
    uint8_t latchBlanking = 1;
    bool use20MHz = 0;
    void print(String prefix) {
        ESP_LOGI(__func__, "%s\nBrightness: %d\nDevelopment: %d\nOTA: %d\nGithub: %d\nSigned FW Only: %d\n", prefix.c_str(), brightness, development, ota, github, signedFWOnly);
    }
};

// Partition struct for verifying firmware is intended for this device
struct PanelPartition
{
    char cookie[32];
    char reserved[224]; // Reserved for future use, total of 256 bytes
};

class Panel {
    public:
        Panel();
        void init();

    private:
        // Objects
        MatrixPanel_I2S_DMA *dma_display;
        AsyncWebServer server;
        HTTPClient https;
        WiFiClientSecure client;
        WiFiManager wifiManager;
        PanelPrefs panelPrefs;

        // Variables
        String serial;
        bool wifiReady;

        // UI Components
        ESPDash dashboard;
        Card otaToggle;
        Card GHUpdateToggle;
        Card developmentToggle;
        Card signedFWOnlyToggle;
        Statistic fwVersion;
        Card brightnessSlider;
        Card emojiInput;
        Card textInput;
        Card latchSlider;
        Card use20MHzToggle;
        Card rebootButton;
        Card resetWifiButton;
        Card crashMe;
        Tab systemTab;
        Tab developerTab;

        // FreeRTOS Tasks
        TaskHandle_t checkForUpdatesTask;
        TaskHandle_t checkForOTATask;
        TaskHandle_t printMemTask;
        Preferences prefs;

        // Functions
        void showDebug();
        void showCoordinates();
        void showTestSequence();
        void setBrightness(uint8_t brightness);
        uint8_t getBrightness();
        void setDevelopment(bool development);
        void setOTA(bool ota);
        void setGHUpdate(bool github);
        void setSignedFWOnly(bool signedFWOnly);
        bool initPrefs();
        void initUpdates();
        bool initDisplay();
        bool initWifi();
        void initUI();
        void initAPI();
        void checkForUpdates();
        void checkForOTA();
        void updatePrefs();
        void printMem();

        esp_err_t setEmoji(const char *emoji);
        esp_err_t setText(const char *text);
};

#endif