#ifndef CONFIG_H
#define CONFIG_H

// SW Version (Github Actions automatically sets this)
#ifndef FW_VERSION
  #define FW_VERSION "DEV"
#endif

// Repo for automatic updates (Github Actions automatically sets this)
#ifndef REPO_URL
  #define REPO_URL "elliotmatson/esp32-hub75-status"
#endif

// Github polling interval
#define CHECK_FOR_UPDATES_INTERVAL 60 // Seconds

// Signature for cube firmware
#define MAGIC_COOKIE "status_FW"

// Cube Hostname
#define HOSTNAME "status"

// Panel Settings
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64

// NTP server
#define NTP_SERVER "pool.ntp.org"

// API Endpoint
#define API_ENDPOINT "/api"

// PCB pinouts
#define R1_PIN 4
#define G1_PIN 15
#define B1_PIN 5
#define R2_PIN 19
#define G2_PIN 18
#define B2_PIN 22
#define A_PIN 32
#define B_PIN 23
#define C_PIN 33
#define D_PIN 14
#define E_PIN 21
#define LAT_PIN 27
#define OE_PIN 26
#define CLK_PIN 25
#define USR_LED 2
#define CONTROL_BUTTON 0

// Colors
#define BLACK 0x0000
#define WHITE 0xFFFF
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define YELLOW 0xFFE0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define ORANGE 0xFD20

#endif // CONFIG_H