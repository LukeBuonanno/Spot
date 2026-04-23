# ESP32-S3 Spotify Controller (GC9A01 Round TFT)

A handheld Spotify controller using an ESP32-S3, a 1.28" round GC9A01 TFT display, and a 4-button module, all housed in a 3D-printed chassis with a Windows 98–style interface.

## Notes

* This device controls Spotify playback but does not output audio
* Requires an active Spotify session on another device
* Spotify Premium is required for API playback control

## Hardware Requirements

* ESP32-S3 Dev Board
* 1.28" Round TFT LCD Display (240x240, GC9A01 driver)
* 4-bit Independent Button Module (micro switch board)
* Jumper wires
* 3D printed chassis
* USB cable for power and programming



## Physical Assembly

### 1. Install the Display

* Press the round TFT display into the front ring of the chassis
* Ensure it sits flush and centered

### 2. Wire the Display (SPI)

Connect the display to the ESP32-S3 as follows:

| Display Pin | ESP32-S3 Pin |
| ----------- | ------------ |
| VCC         | 3.3V         |
| GND         | GND          |
| SCL (CLK)   | GPIO 36      |
| SDA (MOSI)  | GPIO 35      |
| DC          | GPIO 12      |
| CS          | GPIO 14      |
| RST         | GPIO 13      |



### 4. Wire the Button Module

The 4-button module typically has:

* VCC
* GND
* 4 signal outputs (one per button)

Connect as follows:

| Button Function | ESP32 Pin |
| --------------- | --------- |
| Play/Pause      | GPIO 4    |
| Mode/Menu       | GPIO 5    |
| Previous        | GPIO 6    |
| Next            | GPIO 7    |

Then:

* Connect **GND on the module → ESP32 GND**
* If the module requires power:

  * Connect **VCC → 3.3V**


### 5. Final step: Install the ESP32-S3

* Insert the ESP32-S3 into the back of the chassis with pins facing inward.
* Connect USB power



## Software Setup

### 1. Install Arduino IDE

Install the latest Arduino IDE.

### 2. Install ESP32 Board Package

In Arduino IDE:

File → Preferences → Additional Board URLs:


https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

Then:

* Open Boards Manager
* Install "ESP32 by Espressif Systems"

### 3. Select Board

* Board: ESP32S3 Dev Module
* Select the correct COM port

### 4. Install Required Libraries

Install these from Library Manager:

* ArduinoJson
* Adafruit GFX
* Adafruit GC9A01A

### 5. Add Project Files

Place these files in your project folder:

* micross7pt7b.h
* win98_clouds.h
* warning.h
* stop.h


## Spotify Setup

### 1. Create a Spotify App

Go to the Spotify Developer Dashboard and create an app.

You will get:

* Client ID
* Client Secret

### 2. Generate Refresh Token

Use OAuth with scopes:

user-read-playback-state
user-modify-playback-state

### 3. Insert Credentials

Update in your code:


String clientId = "YOUR_CLIENT_ID";
String clientSecret = "YOUR_CLIENT_SECRET";
String refreshToken = "YOUR_REFRESH_TOKEN";




## WiFi Setup

Update:

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";


## Captive Portal (Important)

This code includes automatic login for a network portal.

If you are not using that network:

Remove or comment out:


authenticateCaptivePortal();


Or replace:


const char* portalUser = "...";
const char* portalPass = "...";


## Uploading the Code

1. Connect ESP32-S3 via USB
2. Select correct board and port
3. Click Upload

If upload fails:

* Hold the BOOT button while uploading


## Controls
(From left to right)
| Button   | Action             |
| -------- | ------------------ |
| Previous | Previous track     |
| Next     | Next track         |
| Play     | Play / Pause       |
| Mode     | Open playlist menu |


## Playlist Menu

* Previous / Next: Navigate playlists
* Play: Select playlist
* Mode: Exit menu


## Troubleshooting

### Screen not working

* Verify SPI wiring (SCL, SDA, CS, DC, RST)
* Confirm pins match code

### Spotify not responding

* Ensure a device is actively playing (phone, computer, etc.)
* Verify token and credentials

### Error screen appears

* No active Spotify device
* WiFi or API connection issue

### Buttons not working

* Check wiring from module outputs to GPIO pins
* Ensure GND is connected
* Confirm module logic matches pull-up configuration

