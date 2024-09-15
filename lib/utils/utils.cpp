#include "utils.h"

// SPIRAM Allocator for ArduinoJSON
struct SpiRamAllocator
{
    void *allocate(size_t size)
    {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }

    void deallocate(void *pointer)
    {
        heap_caps_free(pointer);
    }

    void *reallocate(void *ptr, size_t new_size)
    {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};
using SpiRamJsonDocument = BasicJsonDocument<SpiRamAllocator>;

// for signing FW on Github
const __attribute__((section(".rodata_custom_desc"))) PanelPartition panelPartition = {MAGIC_COOKIE};

// Create a new Panel object with optional devMode
Panel::Panel() : 
    server(80),
    serial(String(ESP.getEfuseMac() % 0x1000000, HEX)),
    wifiReady(false),
    dashboard(&server),
    otaToggle(&dashboard, BUTTON_CARD, "OTA Update Enabled"),
    GHUpdateToggle(&dashboard, BUTTON_CARD, "Github Update Enabled"),
    developmentToggle(&dashboard, BUTTON_CARD, "Use Development Builds"),
    signedFWOnlyToggle(&dashboard, BUTTON_CARD, "Signed FW only"),
    fwVersion(&dashboard, "Firmware Version", FW_VERSION),
    brightnessSlider(&dashboard, SLIDER_CARD, "Brightness:", "", 0, 255),
    emojiInput(&dashboard, TEXT_INPUT_CARD, "Emoji", "Enter text here"),
    textInput(&dashboard, TEXT_INPUT_CARD, "Text Input", "Enter text here"),
    latchSlider(&dashboard, SLIDER_CARD, "Latch Blanking:", "", 1, 4),
    use20MHzToggle(&dashboard, BUTTON_CARD, "Use 20MHz Clock"),
    rebootButton(&dashboard, BUTTON_CARD, "Reboot Panel"),
    resetWifiButton(&dashboard, BUTTON_CARD, "Reset Wifi"),
    crashMe(&dashboard, BUTTON_CARD, "Crash Panel"),
    systemTab(&dashboard, "System"),
    developerTab(&dashboard, "Development")
{
}

// initialize all cube tasks and functions
void Panel::init()
{
    pinMode(CONTROL_BUTTON, INPUT_PULLUP);
    Serial.begin(115200);
    initPrefs();
    initDisplay();
    initWifi();

    showDebug();
    delay(5000);
    // blank panel after debug
    dma_display->fillScreenRGB888(0, 0, 0);
    //

    initAPI();
    initUI();
    initUpdates();

    // Start the task to show the selected pattern
    xTaskCreate(
        [](void *o)
        { static_cast<Panel *>(o)->printMem(); }, // This is disgusting, but it works
        "Memory Printer",                        // Name of the task (for debugging)
        3000,                                    // Stack size (bytes)
        this,                                    // Parameter to pass
        1,                                       // Task priority
        &printMemTask                            // Task handle
    );
}

// Initialize Preferences Library
bool Panel::initPrefs()
{
    bool status = prefs.begin("panel");

    if ((!prefs.isKey("panelPrefs")) || (prefs.getBytesLength("panelPrefs") != sizeof(PanelPrefs))) {
        this->panelPrefs.print("No valid preferences found, creating new");
        prefs.putBytes("panelPrefs", &panelPrefs, sizeof(PanelPrefs));
    }
    prefs.getBytes("panelPrefs", &panelPrefs, sizeof(PanelPrefs));
    this->panelPrefs.print("Loaded Preferences");
    return status;
}

// Initialize update methods, setup check tasks
void Panel::initUpdates()
{
    this->setOTA(this->panelPrefs.ota);
    this->setGHUpdate(this->panelPrefs.github);
}

// Initialize display driver
bool Panel::initDisplay()
{
    bool status = false;
    ESP_LOGI(__func__,"Configuring HUB_75");
    HUB75_I2S_CFG::i2s_pins _pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};
    HUB75_I2S_CFG mxconfig(PANEL_WIDTH, PANEL_HEIGHT, 1, _pins);
    if(panelPrefs.use20MHz) {
        mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_20M;
    } else {
        mxconfig.i2sspeed = HUB75_I2S_CFG::HZ_10M;
    }
    mxconfig.clkphase = false;
    dma_display = new MatrixPanel_I2S_DMA(mxconfig);
    dma_display->setLatBlanking(panelPrefs.latchBlanking);

    // Allocate memory and start DMA display
    if (dma_display->begin()) {
        status = true;
    } else {
        ESP_LOGE(__func__, "****** !KABOOM! I2S memory allocation failed ***********");
    }
    setBrightness(this->panelPrefs.brightness);
    return status;
}

// Initialize wifi and prompt for connection if needed
bool Panel::initWifi()
{
    ESP_LOGI(__func__,"Connecting to WiFi...");
    wifiManager.setHostname("cube");
    wifiManager.setClass("invert");
    wifiManager.setAPCallback([&](WiFiManager *myWiFiManager)
        {
            dma_display->fillScreen(BLACK);
            dma_display->setTextColor(WHITE);
            dma_display->setCursor(0, 0);
            dma_display->printf("\n\nConnect to\n   WiFi\n\nSSID: %s", myWiFiManager->getConfigPortalSSID().c_str());
        });

    bool status = wifiManager.autoConnect("Panel");

    // Set up NTP
    long gmtOffset_sec = 0;
    int daylightOffset_sec = 0;

    // get GMT offset from public API
    WiFiClient client;
    HTTPClient http;
    http.begin(client, "http://worldtimeapi.org/api/ip");
    int httpCode = http.GET();
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(1024);
            deserializeJson(doc, payload);
            gmtOffset_sec = doc["raw_offset"].as<int>();
            daylightOffset_sec = doc["dst_offset"].as<int>();
        }
    }
    http.end();

    // Set time via NTP
    configTime(gmtOffset_sec, daylightOffset_sec, NTP_SERVER);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        ESP_LOGE(__func__, "Failed to obtain time");
    }
    ESP_LOGI(__func__,"Time set: %s", asctime(&timeinfo));

    // Set up web server
    this->server.begin();

    this->wifiReady = true;

    ESP_LOGI(__func__,"IP address: ");
    ESP_LOGI(__func__,"%s",WiFi.localIP().toString().c_str());
    MDNS.begin(HOSTNAME);
    return status;
}

// initialize Panel UI Elements
void Panel::initUI()
{
    dashboard.setTitle("Status Panel");

    this->otaToggle.attachCallback([&](int value)
        {
            this->setOTA(value);
            this->otaToggle.update(value);
            this->dashboard.sendUpdates(); 
        });
    this->developmentToggle.attachCallback([&](int value)
        {
            this->setDevelopment(value);
            this->developmentToggle.update(value);
            this->dashboard.sendUpdates(); 
        });
    this->GHUpdateToggle.attachCallback([&](int value)
        {
            this->setGHUpdate(value);
            this->GHUpdateToggle.update(value);
            this->dashboard.sendUpdates(); 
        });
    this->signedFWOnlyToggle.attachCallback([&](int value)
                                            {
            this->setSignedFWOnly(value);
            this->signedFWOnlyToggle.update(value);
            this->dashboard.sendUpdates(); });
    brightnessSlider.attachCallback([&](int value)
                                     {
            this->setBrightness(value);
            this->brightnessSlider.update(value);
            this->dashboard.sendUpdates(); });
    emojiInput.attachCallback([&](const char *value)
                            {
            this->setEmoji(value);
                             });
    textInput.attachCallback([&](const char *value)
                            {
            this->setText(value);
                             });
    latchSlider.attachCallback([&](int value)
                                     {
            this->dma_display->setLatBlanking(value);
            this->panelPrefs.latchBlanking = value;
            this->updatePrefs();
            this->latchSlider.update(value);
            this->dashboard.sendUpdates(); });
    use20MHzToggle.attachCallback([&](int value)
                                  {
            this->panelPrefs.use20MHz = value;
            this->updatePrefs();
            this->use20MHzToggle.update(value);
            this->dashboard.sendUpdates(); });
    rebootButton.attachCallback([&](int value)
                                {
            ESP_LOGI(__func__,"Rebooting...");
            ESP.restart();
            this->dashboard.sendUpdates(); });
    resetWifiButton.attachCallback([&](int value)
                                     {
            ESP_LOGI(__func__,"Resetting WiFi...");
            wifiManager.resetSettings();
            ESP.restart();
            this->dashboard.sendUpdates(); });
    crashMe.attachCallback([&](int value)
                                     {
            ESP_LOGI(__func__,"Crashing...");
            int *p = NULL;
            *p = 80;
            this->dashboard.sendUpdates(); });
    this->otaToggle.update(this->panelPrefs.ota);
    this->developmentToggle.update(this->panelPrefs.development);
    this->GHUpdateToggle.update(this->panelPrefs.github);
    this->brightnessSlider.update(this->panelPrefs.brightness);
    this->signedFWOnlyToggle.update(this->panelPrefs.signedFWOnly);
    this->latchSlider.update(this->panelPrefs.latchBlanking);
    this->use20MHzToggle.update(this->panelPrefs.use20MHz);
    this->rebootButton.update(true);
    this->resetWifiButton.update(true);

    this->rebootButton.setTab(&systemTab);
    this->resetWifiButton.setTab(&systemTab);
    this->otaToggle.setTab(&developerTab);
    this->developmentToggle.setTab(&developerTab);
    this->GHUpdateToggle.setTab(&developerTab);
    this->signedFWOnlyToggle.setTab(&developerTab);
    this->crashMe.setTab(&developerTab);
    this->latchSlider.setTab(&developerTab);
    this->use20MHzToggle.setTab(&developerTab);

    dashboard.sendUpdates();

    MDNS.addService("http", "tcp", 80);
}

/**
 * The function initializes the API and creates a JSON response containing information about patterns.
 */
void Panel::initAPI()
{
    char uri[128];

    // test endpoint
    sprintf(uri, "%s/v1/test", API_ENDPOINT);
    server.on(uri, HTTP_GET, [&](AsyncWebServerRequest *request)
              { request->send(200, "application/json", "{\"Hello\": \"world\"}"); });

    // get/set brightness in JSON
    sprintf(uri, "%s/v1/brightness", API_ENDPOINT);
    server.on(uri, HTTP_GET, [&](AsyncWebServerRequest *request)
              {
        request->send(200, "application/json", String("{\"brightness\":") + this->getBrightness() + "}"); });
    server.on(uri, HTTP_POST, [&](AsyncWebServerRequest *request)
              {
        //print request
        ESP_LOGI(__func__,"POST %s", request->url().c_str());
        if (request->hasArg("brightness"))
        {
            this->setBrightness(request->arg("brightness").toInt());
            request->send(200, "application/json", String("{\"brightness\":") + this->getBrightness() + "}");
        }
        else
        {
            request->send(400, "application/json", "{\"error\": \"No brightness parameter\"}");
        }
    });

    // get/set Emoji with JSON, query string, or POST
    sprintf(uri, "%s/v1/emoji", API_ENDPOINT);
    server.on(uri, HTTP_GET, [&](AsyncWebServerRequest *request)
              {
            request->send(200, "application/json", String("{\"emoji\":\"") + request->arg("emoji") + "\"}");
    });
    server.on(uri, HTTP_POST, [&](AsyncWebServerRequest *request)
              {
        //print request
        ESP_LOGI(__func__,"POST %s", request->url().c_str());
        if (request->hasArg("emoji"))
        {
            if (this->setEmoji(request->arg("emoji").c_str()))
            {
                request->send(200, "application/json", String("{\"emoji\":\"") + request->arg("emoji") + "\"");
            }
            else
            {
                request->send(400, "application/json", "{\"error\": \"Invalid emoji\"}");
            }
        }
        else
        {
            request->send(400, "application/json", "{\"error\": \"No emoji parameter\"}");
        }
    });

    // get/set text with JSON, query string, or POST
    sprintf(uri, "%s/v1/text", API_ENDPOINT);
    server.on(uri, HTTP_GET, [&](AsyncWebServerRequest *request)
              {
        request->send(200, "application/json", String("{\"text\":\"") + request->arg("text") + "\"}");
    });
    server.on(uri, HTTP_POST, [&](AsyncWebServerRequest *request)
              {
        //print request
        ESP_LOGI(__func__,"POST %s", request->url().c_str());
        if (request->hasArg("text"))
        {
            this->setText(request->arg("text").c_str());
            request->send(200, "application/json", String("{\"text\":\"") + request->arg("text") + "\"");
        }
        else
        {
            request->send(400, "application/json", "{\"error\": \"No text parameter\"}");
        }
    });


    // redirect to docs on api root request
    server.on(API_ENDPOINT, HTTP_GET, [&](AsyncWebServerRequest *request)
              { request->redirect("https://github.com/elliotmatson/LED_Cube"); });
}

// set brightness of display
void Panel::setBrightness(uint8_t brightness)
{
    this->panelPrefs.brightness = brightness;
    this->updatePrefs();
    dma_display->setBrightness8(brightness);
}

// get brightness of display
uint8_t Panel::getBrightness()
{
    return this->panelPrefs.brightness;
}

// set OTA enabled/disabled
void Panel::setOTA(bool ota)
{
    panelPrefs.ota = ota;
    this->updatePrefs();
    if(ota) {
        ESP_LOGI(__func__,"Starting OTA");
        ArduinoOTA.setHostname(HOSTNAME);
        ArduinoOTA
            .onStart([&]()
                     {
                    String type;
                    if (ArduinoOTA.getCommand() == U_FLASH)
                        type = "sketch";
                    else // U_SPIFFS
                        type = "filesystem";

                    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
                    ESP_LOGI(__func__,"Start updating %s", type.c_str());
                    dma_display->fillScreenRGB888(0, 0, 0);
                    dma_display->setFont(NULL);
                    dma_display->setCursor(6, 21);
                    dma_display->setTextColor(0xFFFF);
                    dma_display->setTextSize(3);
                    dma_display->print("OTA"); })
            .onEnd([&]()
                   {
                    ESP_LOGI(__func__,"End"); 
                    for(int i = getBrightness(); i > 0; i=i-3) {
                        dma_display->setBrightness8(max(i, 0));
                    } })
            .onProgress([&](unsigned int progress, unsigned int total)
                        { 
                    this->dashboard.sendUpdates();
                    ESP_LOGI(__func__,"Progress: %u%%\r", (progress / (total / 100)));

                    if (this->panelPrefs.signedFWOnly && progress == total)
                    {
                        PanelPartition newPanelPartition;
                        esp_partition_read(esp_ota_get_next_update_partition(NULL), sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t), &newPanelPartition, sizeof(newPanelPartition));
                        ESP_LOGI(__func__,"Checking for Panel FW Signature: \nNew:%s\nold:%s", newPanelPartition.cookie, panelPartition.cookie);
                        if (strcmp(newPanelPartition.cookie, panelPartition.cookie))
                            Update.abort();
                    }
                    // Draw a progress bar on edges of the display
                    int i = map(progress, 0, total, 0, 256);
                    dma_display->drawFastHLine(0, 0, constrain(i, 0, 63), 0xFFFF);
                    dma_display->drawFastVLine(63, 0, constrain(i - 64, 0, 63), 0xFFFF);
                    dma_display->drawFastHLine(63 - constrain(i - 128, 0, 63), 63, constrain(i - 128, 0, 63), 0xFFFF);
                    dma_display->drawFastVLine(0, 63 - constrain(i - 192, 0, 63), constrain(i - 192, 0, 63), 0xFFFF);
                    
                     })
            .onError([&](ota_error_t error)
                     {
                    ESP_LOGE(__func__,"Error[%u]: ", error);
                    if (error == OTA_AUTH_ERROR) ESP_LOGE(__func__,"Auth Failed");
                    else if (error == OTA_BEGIN_ERROR) ESP_LOGE(__func__,"Begin Failed");
                    else if (error == OTA_CONNECT_ERROR) ESP_LOGE(__func__,"Connect Failed");
                    else if (error == OTA_RECEIVE_ERROR) ESP_LOGE(__func__,"Receive Failed");
                    else if (error == OTA_END_ERROR) ESP_LOGE(__func__,"End Failed"); });

        ArduinoOTA.begin();

        xTaskCreate(
            [](void* o){ static_cast<Panel*>(o)->checkForOTA(); }, // This is disgusting, but it works
            "Check For OTA", // Name of the task (for debugging)
            6000,            // Stack size (bytes)
            this,            // Parameter to pass
            5,               // Task priority
            &checkForOTATask // Task handle
        );
    } else
    {
        ESP_LOGI(__func__,"OTA Disabled");
        ArduinoOTA.end();
        if(checkForOTATask){
            vTaskDelete(checkForOTATask);
        }
    }
}

/**
 * The function `setGHUpdate` enables or disables Github updates for a Panel object and performs
 * necessary actions based on the update status.
 * 
 * @param github The parameter "github" is a boolean value that indicates whether GitHub updates are
 * enabled or disabled.
 */
void Panel::setGHUpdate(bool github)
{
    panelPrefs.github=github;
    this->updatePrefs();
    if(github) {
        ESP_LOGI(__func__,"Github Update enabled...");
        httpUpdate.onStart([&]()
                           {
            ESP_LOGI(__func__,"Start updating");
            dma_display->fillScreenRGB888(0, 0, 0);
            dma_display->setFont(NULL);
            dma_display->setCursor(6, 21);
            dma_display->setTextColor(0xFFFF);
            dma_display->setTextSize(3);
            dma_display->print("GHA"); });
        httpUpdate.onEnd([&]()
        { 
            ESP_LOGI(__func__,"End"); 
            for(int i = getBrightness(); i > 0; i=i-3) {
                dma_display->setBrightness8(max(i, 0));
            }
        });
        httpUpdate.onProgress([&](unsigned int progress, unsigned int total)
                              { 
            ESP_LOGI(__func__,"Progress: %u%%\r", (progress / (total / 100)));

            if (this->panelPrefs.signedFWOnly && progress == total)
            {
                PanelPartition newPanelPartition;
                esp_partition_read(esp_ota_get_next_update_partition(NULL), sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t), &newPanelPartition, sizeof(newPanelPartition));
                ESP_LOGI(__func__,"Checking for Panel FW Signature: \nNew:%s\nold:%s", newPanelPartition.cookie, panelPartition.cookie);
                if (strcmp(newPanelPartition.cookie, panelPartition.cookie))
                    Update.abort();
            }

            // Draw a progress bar on edges of the display
            int i = map(progress, 0, total, 0, 256);
            dma_display->drawFastHLine(0, 0, constrain(i, 0, 63), 0xFFFF);
            dma_display->drawFastVLine(63, 0, constrain(i - 64, 0, 63), 0xFFFF);
            dma_display->drawFastHLine(63 - constrain(i - 128, 0, 63), 63, constrain(i - 128, 0, 63), 0xFFFF);
            dma_display->drawFastVLine(0, 63 - constrain(i - 192, 0, 63), constrain(i - 192, 0, 63), 0xFFFF);
        });
            xTaskCreate(
                [](void *o)
                { static_cast<Panel *>(o)->checkForUpdates(); }, // This is disgusting, but it works
                "Check For Updates",                             // Name of the task (for debugging)
                8000,                                            // Stack size (bytes)
                this,                                            // Parameter to pass
                5,                                               // Task priority
                &checkForUpdatesTask                             // Task handle
            );
    } else {
        ESP_LOGI(__func__,"Github Updates Disabled");
        if(checkForUpdatesTask) {
            vTaskDelete(checkForUpdatesTask);
        }
    }
}

// set dev mode
void Panel::setDevelopment(bool development)
{ 
    panelPrefs.development = development;
    this->updatePrefs();
}

// set signedFWOnly
void Panel::setSignedFWOnly(bool signedFWOnly)
{ 
    panelPrefs.signedFWOnly = signedFWOnly;
    this->updatePrefs();
}

// update preferences stored in NVS
void Panel::updatePrefs()
{
    this->panelPrefs.print("Updating Preferences...");
    prefs.putBytes("panelPrefs", &panelPrefs, sizeof(PanelPrefs));
}

// shows debug info on display
void Panel::showDebug()
{
    dma_display->fillScreenRGB888(0, 0, 0);
    dma_display->setCursor(0, 0);
    dma_display->setTextColor(0xFFFF);
    dma_display->setTextSize(1);
    dma_display->printf("%s\nH%s\nS%s\nSN: %s\nH: %d\nP: %d",
                        WiFi.localIP().toString().c_str(),
                        prefs.getString("HW").c_str(),
                        FW_VERSION,
                        serial.c_str(),
                        ESP.getFreeHeap(),
                        ESP.getFreePsram());
}

// shows coordinates on display for debugging
void Panel::showCoordinates() {
    dma_display->fillScreenRGB888(0, 0, 0);
    dma_display->setTextColor(RED);
    dma_display->setTextSize(1);
    dma_display->drawFastHLine(0, 0, 192, 0x4208);
    dma_display->drawFastHLine(0, 63, 192, 0x4208);
    for(int i = 0; i < 3; i++) {
        int x0 = i*64;
        int y0 = 0;
        dma_display->drawFastVLine(i * 64, 0, 64, 0x4208);
        dma_display->drawFastVLine((i * 64) + 63, 0, 64, 0x4208);
        dma_display->drawPixel(x0, y0, RED);
        dma_display->drawPixel(x0, y0+63, GREEN);
        dma_display->drawPixel(x0+63, y0, BLUE);
        dma_display->drawPixel(x0+63, y0+63, YELLOW);
        dma_display->setTextColor(RED);
        dma_display->setCursor(x0 + 1, y0 + 1);
        dma_display->printf("%d,%d", x0, y0);
        dma_display->setTextColor(GREEN);
        dma_display->setCursor(x0+1, y0+55);
        dma_display->printf("%d,%d", x0, y0+63);
    }
    dma_display->setTextColor(BLUE);
    dma_display->setCursor(40, 1);
    dma_display->printf("%d,%d", 63, 0);
    dma_display->setCursor(98, 1);
    dma_display->printf("%d,%d", 127, 0);
    dma_display->setCursor(162, 1);
    dma_display->printf("%d,%d", 191, 0);
    dma_display->setTextColor(YELLOW);
    dma_display->setCursor(34, 55);
    dma_display->printf("%d,%d", 63, 63);
    dma_display->setCursor(92, 47);
    dma_display->printf("%d,%d", 127, 63);
    dma_display->setCursor(156, 47);
    dma_display->printf("%d,%d", 191, 63);
}

// Show a basic test sequence for testing panels
void Panel::showTestSequence()
{
  dma_display->fillScreenRGB888(255, 0, 0);
  delay(500);
  dma_display->fillScreenRGB888(0, 255, 0);
  delay(500);
  dma_display->fillScreenRGB888(0, 0, 255);
  delay(500);
  dma_display->fillScreenRGB888(255, 255, 255);
  delay(500);
  dma_display->fillScreenRGB888(0, 0, 0);

  for (uint8_t i = 0; i < 64; i++)
  {
    for (uint8_t j = 0; j < 64; j++)
    {
      dma_display->drawPixelRGB888(i, j, 255, 255, 255);
    }
    delay(50);
  }
  for (uint8_t i = 0; i < 64; i++)
  {
    for (uint8_t j = 0; j < 64; j++)
    {
      dma_display->drawPixelRGB888(i, j, 0, 0, 0);
    }
    delay(50);
  }
  for (uint8_t j = 0; j < 64; j++)
  {
    for (uint8_t i = 0; i < 64; i++)
    {
      dma_display->drawPixelRGB888(i, j, 255, 255, 255);
    }
    delay(50);
  }
  for (uint8_t j = 0; j < 64; j++)
  {
    for (uint8_t i = 0; i < 64; i++)
    {
      dma_display->drawPixelRGB888(i, j, 0, 0, 0);
    }
    delay(50);
  }
}

// Task to check for updates
void Panel::checkForUpdates()
{
    for (;;)
    {
        HTTPClient http;
        WiFiClientSecure client;
        client.setCACertBundle(rootca_crt_bundle_start);

        String firmwareUrl = "";
        ESP_LOGI(__func__,"Branch = %s", this->panelPrefs.development ? "development" : "main");
        String boardFile = "/esp32.bin";
        if(this->panelPrefs.development) {
            // https://api.github.com/repos/elliotmatson/LED_Cube/releases
            String jsonUrl = String("https://api.github.com/repos/") + REPO_URL + String("/releases");
            ESP_LOGI(__func__,"%s", jsonUrl.c_str());
            http.useHTTP10(true);
            if (http.begin(client, jsonUrl)) {
                SpiRamJsonDocument filter(200);
                filter[0]["name"] = true;
                filter[0]["prerelease"] = true;
                filter[0]["assets"] = true;
                filter[0]["published_at"] = true;
                http.GET();
                SpiRamJsonDocument doc(4096);
                deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
                JsonArray releases = doc.as<JsonArray>();
                int newestPrereleaseIndex = -1;
                String newestPrereleaseDate = "";
                for (int i=0; i<releases.size(); i++)
                {
                    JsonObject release = releases[i].as<JsonObject>();
                    if (release["prerelease"].as<bool>() && release["published_at"].as<String>() > newestPrereleaseDate)
                    {
                        newestPrereleaseIndex = i;
                        newestPrereleaseDate = release["published_at"].as<String>();
                    }
                }
                JsonObject newestPrerelease = releases[newestPrereleaseIndex].as<JsonObject>();
                ESP_LOGI(__func__,"Newest Prerelease: %s  date:%s", newestPrerelease["name"].as<String>().c_str(), newestPrerelease["published_at"].as<String>().c_str());
                // https://github.com/elliotmatson/LED_Cube/releases/download/v0.2.3/esp32.bin
                firmwareUrl = String("https://github.com/") + REPO_URL + String("/releases/download/") + newestPrerelease["name"].as<String>() + boardFile;
                http.end();
            }
        } else {
            firmwareUrl = String("https://github.com/") + REPO_URL + String("/releases/latest/download/") + boardFile;
        }
        ESP_LOGI(__func__,"%s", firmwareUrl.c_str());

        if (http.begin(client, firmwareUrl) && firmwareUrl != "") {
            int httpCode = http.sendRequest("HEAD");
            if (httpCode < 300 || httpCode > 400 || (http.getLocation().indexOf(String(FW_VERSION)) > 0) || (firmwareUrl.indexOf(String(FW_VERSION)) > 0))
            {
                ESP_LOGI(__func__,"Not updating from (sc=%d): %s", httpCode, http.getLocation().c_str());
                http.end();
            }
            else
            {
                ESP_LOGI(__func__,"Updating from (sc=%d): %s", httpCode, http.getLocation().c_str());

                httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
                t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);

                switch (ret)
                {
                case HTTP_UPDATE_FAILED:
                    ESP_LOGE(__func__,"Http Update Failed (Error=%d): %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                    break;

                case HTTP_UPDATE_NO_UPDATES:
                    ESP_LOGI(__func__,"No Update!");
                    break;

                case HTTP_UPDATE_OK:
                    ESP_LOGI(__func__,"Update OK!");
                    break;
                }
            }
        }
        vTaskDelay((CHECK_FOR_UPDATES_INTERVAL * 1000) / portTICK_PERIOD_MS);
    }
}

// Task to handle OTA updates
void Panel::checkForOTA()
{
    for (;;)
    {
        ArduinoOTA.handle();
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

void Panel::printMem()
{
    for (;;) {
        ESP_LOGI(__func__, "Free Heap: %d / %d, Used PSRAM: %d / %d", ESP.getFreeHeap(), ESP.getHeapSize(), heap_caps_get_total_size(MALLOC_CAP_SPIRAM) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
        ESP_LOGI(__func__, "Largest free block in Heap: %d, PSRAM: %d", ESP.getMaxAllocHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        /*char *buf = new char[2048];
        vTaskGetRunTimeStats(buf);
        Serial.println(buf);
        delete[] buf;
        buf = new char[2048];
        vTaskList(buf);
        Serial.println(buf);
        delete[] buf;*/
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}

esp_err_t Panel::setEmoji(const char *emoji)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(__func__, "Emoji Input: %s", emoji);
    // print emoji as hex
    for (int i = 0; i < strlen(emoji); i++)
    {
        ESP_LOGI(__func__, "0x%02X ", emoji[i]);
    }
    dma_display->fillRect(0, 0, 64, 32, BLACK);

    uint8_t len = strlen(emoji);
    // detect utf-8 encoding, and convert to code points
    if ((uint8_t)emoji[0] > 0x7F)
    {
        // count code points in string
        uint8_t codepointCount = 0;
        for (int i = 0; i < len; i++)
        {
            if ((uint8_t)emoji[i] >= 0xC0 && (uint8_t)emoji[i] <= 0xDF)
            {
                i++;
            }
            else if ((uint8_t)emoji[i] >= 0xE0 && (uint8_t)emoji[i] <= 0xEF)
            {
                i += 2;
            }
            else if ((uint8_t)emoji[i] >= 0xF0 && (uint8_t)emoji[i] <= 0xF7)
            {
                i += 3;
            }
            codepointCount++;
        }
        ESP_LOGI(__func__, "Codepoint Count: %d", codepointCount);

        // convert utf-8 to code points
        uint32_t codepoints[codepointCount];
        uint8_t codepointIndex = 0;
        for (int i = 0; i < len; i++)
        {
            if ((uint8_t)emoji[i] >= 0xC0 && (uint8_t)emoji[i] <= 0xDF)
            {
                codepoints[codepointIndex] = (emoji[i] & 0x1F) << 6;
                codepoints[codepointIndex] |= (emoji[i + 1] & 0x3F);
                i++;
            }
            else if ((uint8_t)emoji[i] >= 0xE0 && (uint8_t)emoji[i] <= 0xEF)
            {
                codepoints[codepointIndex] = (emoji[i] & 0x0F) << 12;
                codepoints[codepointIndex] |= (emoji[i + 1] & 0x3F) << 6;
                codepoints[codepointIndex] |= (emoji[i + 2] & 0x3F);
                i += 2;
            }
            else if ((uint8_t)emoji[i] >= 0xF0 && (uint8_t)emoji[i] <= 0xF7)
            {
                codepoints[codepointIndex] = (emoji[i] & 0x07) << 18;
                codepoints[codepointIndex] |= (emoji[i + 1] & 0x3F) << 12;
                codepoints[codepointIndex] |= (emoji[i + 2] & 0x3F) << 6;
                codepoints[codepointIndex] |= (emoji[i + 3] & 0x3F);
                i += 3;
            }
            codepointIndex++;
        }

        // print code points
        for (int i = 0; i < codepointCount; i++)
        {
            ESP_LOGI(__func__, "Codepoint: U+%05X", codepoints[i]);
        }

        // Add first codepoint to emoji string
        String codepointsString = String(codepoints[0], HEX);
        int i = 1;
        // detect following skintone modifiers
        if (codepointCount > 1 && codepoints[1] >= 0x1F3FB && codepoints[1] <= 0x1F3FF)
        {
            codepointsString += "_" + String(codepoints[1], HEX);
            i++;
        }
        // detect regional indicator symbols
        if (codepointCount > 1 && codepoints[1] >= 0x1F1E6 && codepoints[1] <= 0x1F1FF)
        {
            codepointsString += "_" + String(codepoints[1], HEX);
            i++;
        }

        // detect zero width joiners, add next codepoint(s) to emoji string
        while (i < codepointCount)
        {
            // if zero width joiner, add joiner and next codepoint
            if (codepoints[i] == 0x200D && i + 1 < codepointCount)
            {
                codepointsString += "_200D_" + String(codepoints[i + 1], HEX);
                i += 2;
            }
            // if skintone modifier, add modifier to emoji string
            else if (codepoints[i] >= 0x1F3FB && codepoints[i] <= 0x1F3FF)
            {
                codepointsString += "_" + String(codepoints[i], HEX);
                i++;
            }
            // else, exit loop
            else
            {
                break;
            }
        }
        ESP_LOGI(__func__, "Emoji: %s", codepointsString.c_str());

        // download emoji from codepoint using https://emojiapi.dev/
        // https://emojiapi.dev/api/v1/{emoticon_code_or_name}/{size}.{jpg,png,raw,tiff,webp}
        String emojiUrl = String("https://emojiapi.dev/api/v1/") + codepointsString + "/32.raw";
        ESP_LOGI(__func__, "Emoji URL: %s", emojiUrl.c_str());
        client.setCACertBundle(rootca_crt_bundle_start);

        if (https.begin(client, emojiUrl))
        {
            ESP_LOGI(__func__, "Downloading emoji...");
            int res = https.GET();
            ESP_LOGI(__func__, "HTTP Code: %d", res);
            if (https.getSize() > 0 && res == HTTP_CODE_OK)
            {
                ESP_LOGI(__func__, "Emoji Size: %d", https.getSize());
                for (int i = 0; i < 32; i++)
                {
                    for (int j = 0; j < 32; j++)
                    {
                        if (https.getStream().available() >= 4)
                        {
                            uint8_t r = https.getStream().read();
                            uint8_t g = https.getStream().read();
                            uint8_t b = https.getStream().read();
                            uint8_t a = https.getStream().read();
                            dma_display->drawPixelRGB888(j + 16, i, r * a / 255, g * a / 255, b * a / 255);
                        }
                    }
                }
                this->emojiInput.update(emoji);
            }
            else
            {
                ESP_LOGE(__func__, "Failed to download emoji");
                this->emojiInput.update("Invalid Emoji");
                err = ESP_ERR_NOT_FOUND;
            }
            https.end();
        }
        else
        {
            ESP_LOGE(__func__, "Failed to connect to emoji server");
            this->emojiInput.update("Invalid Emoji");
            err = ESP_ERR_INVALID_STATE;
        }
    }
    else
    {
        this->emojiInput.update("Invalid Emoji");
        err = ESP_ERR_INVALID_ARG;
    }
    this->dashboard.sendUpdates();
    return err;
}

esp_err_t Panel::setText(const char *text)
{
    esp_err_t err = ESP_OK;
    ESP_LOGI(__func__, "Text Input: %s", text);
    dma_display->fillRect(0, 32, 64, 32, BLACK);
    dma_display->setTextColor(WHITE);
    dma_display->setCursor(0, 32);
    dma_display->printf("%s", text);
    this->textInput.update(text);
    this->dashboard.sendUpdates();
    return err;
}