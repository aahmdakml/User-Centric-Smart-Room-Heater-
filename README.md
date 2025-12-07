# Room Temperature Controller – ESP32 + DHT11 + LCD + Web Dashboard

This project is a small IoT system for **monitoring and controlling room temperature** using:

- **ESP32** as the main controller & REST API server  
- **DHT11** sensor for temperature & humidity  
- **LCD 16×2 I2C** for local display  
- **4-channel relay** for controlling:
  - CH1 – empty / reserved for future use  
  - CH2 – PTC heater  
  - CH3 – Fan 1  
  - CH4 – Fan 2  
- A **separate web dashboard** (HTML + CSS + JS) for monitoring and manual control

The system tries to keep the room at around **26 °C** using **automatic control**, but also allows **manual override** from the web UI. When the user stops interacting for a while, the system automatically returns to AUTO mode.

---

## Features

- 🌡️ **Automatic temperature control**
  - Target: **26 °C**
  - Uses **hysteresis** (±0.8 °C) to avoid relay “chattering”
  - Too hot → fans ON, heater OFF  
  - Too cold → heater ON, fans OFF  
  - Comfortable range → all outputs OFF (save energy)

- 📟 **On-device display (LCD 16×2 I2C)**
  - Shows temperature & humidity
  - Shows mode: **AUTO / MAN**
  - Shows heater & fan states in compact format

- 🌐 **REST API on ESP32**
  - `GET /status` → JSON with sensor data & relay states
  - `GET /relay?ch=X&state=Y` → manual control of relays

- 🕹 **Web Dashboard (standalone HTML file)**
  - Realtime monitoring (auto refresh)
  - Manual ON/OFF buttons for all 4 relay channels
  - Mode indicator (AUTO / MANUAL) + manual timeout

- 👤 **User-priority manual mode**
  - Any manual command from the web switches system to **MANUAL mode**
  - AUTO logic is temporarily disabled (user has full control)
  - After a configurable timeout with no user input, system **returns to AUTO**

---

## System Overview

High-level data flow:

1. **DHT11** → reads temperature & humidity → sends to **ESP32**
2. **ESP32**:
   - Runs control logic (AUTO mode)
   - Drives **relay board** (heater + fans)
   - Updates **LCD 16×2** with current state
   - Exposes REST API (`/status`, `/relay`)
3. **Web Dashboard**:
   - Runs on phone / laptop browser (not inside ESP32)
   - Fetches `/status` every few seconds
   - Sends manual control commands via `/relay?ch=X&state=Y`
4. **User device** and **ESP32** are connected to the **same WiFi network** (e.g. phone hotspot)

---

## Hardware

### Components

- 1 × ESP32 Dev Module  
- 1 × DHT11 temperature & humidity sensor  
- 1 × LCD 16×2 with I2C backpack (e.g. address `0x27`)  
- 1 × 4-channel relay module (active LOW)  
- 1 × PTC heater (properly rated for relay & power supply)  
- 2 × DC fans (Fan 1 & Fan 2)  
- 1 × 5 V power supply for relay & loads  
- Breadboard / PCB, jumper wires, etc.

> ⚠️ **Warning – Mains safety**  
> If you use mains-powered heater or high-voltage loads, make sure you know what you’re doing: use proper isolation, fuses, and safe wiring. This project is intended primarily as a **learning / hobby** setup.

### ESP32 Pinout (default configuration)

In the final firmware:

- **DHT11**
  - Data → `GPIO 19`

- **Relays**
  - CH1 → `GPIO 0`  *(note: boot strapping pin; see note below)*  
  - CH2 → `GPIO 14` → PTC heater  
  - CH3 → `GPIO 12` → Fan 1  
  - CH4 → `GPIO 13` → Fan 2  

- **LCD 16×2 I2C**
  - SDA → `GPIO 21`
  - SCL → `GPIO 22`
  - I2C address: `0x27` (common default; verify with I2C scanner if needed)

> ⚠️ **Boot note (GPIO 0)**  
> GPIO 0 is a boot / strapping pin. If CH1 load pulls this pin low at boot, ESP32 may fail to start or enter download mode. If you run into boot issues, consider moving CH1 to a safer GPIO (e.g. 23, 25, 26, 27) and updating the firmware pin config accordingly.

---

## Software Stack

- **ESP32 firmware**
  - Arduino Core for ESP32
  - Libraries:
    - `WiFi.h`
    - `WebServer.h`
    - `DHTesp.h`
    - `LiquidCrystal_I2C.h`

- **Web dashboard**
  - Plain **HTML** for layout
  - **CSS** for styling (cards, gradient background)
  - **JavaScript** (`fetch`) for calling ESP32 REST API

You can develop & flash the firmware with:

- Arduino IDE, PlatformIO, or any other ESP32-friendly environment.

---

## Firmware Overview (ESP32)

The main Arduino sketch (e.g. `room_temp_controller_esp32.ino`) does:

1. Initialize DHT11, relay pins, and the LCD I2C.
2. Connect to WiFi as **station**:

   ```cpp
   const char* ssid     = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
    ````

3. Start a `WebServer server(80);`
4. Register handlers:

   * `/` → plain info text
   * `/status` → JSON status
   * `/relay` → control relays + enable MANUAL mode
5. In `loop()`:

   * `server.handleClient();`
   * Read DHT11 every `DHT_INTERVAL_MS`
   * If in MANUAL mode, check timeout; if expired → switch back to AUTO
   * Run `updateAutoControl()` to adjust heater / fans (AUTO only)
   * Update LCD every `LCD_INTERVAL_MS`

### Control Logic

* Target temperature: `TARGET_TEMP = 26.0`
* Hysteresis: `HYSTERESIS = 0.8`

So:

* **Too hot**

  * Condition: `T > 26.0 + 0.8 = 26.8 °C`
  * Action:

    * Heater (CH2) → OFF
    * Fan1 (CH3)  → ON
    * Fan2 (CH4)  → ON

* **Too cold**

  * Condition: `T < 26.0 - 0.8 = 25.2 °C`
  * Action:

    * Heater (CH2) → ON
    * Fan1 (CH3)  → OFF
    * Fan2 (CH4)  → OFF

* **Comfort zone**

  * Condition: `25.2 °C ≤ T ≤ 26.8 °C`
  * Action:

    * Heater OFF, Fan1 OFF, Fan2 OFF

> The system uses **ACTIVE LOW** relays:
> `LOW` → ON, `HIGH` → OFF.
> This is handled internally by `RELAY_ON` / `RELAY_OFF` constants.

### Modes: AUTO vs MANUAL

* **AUTO mode**

  * `manualMode == false`
  * `updateAutoControl()` is active
  * Relays are controlled by the temperature logic

* **MANUAL mode**

  * Triggered whenever a user calls `/relay?ch=...&state=...`
  * `manualMode == true`
  * `updateAutoControl()` does nothing (user has full control)
  * After `MANUAL_TIMEOUT_MS` (e.g. 3 minutes) with no new user command:

    * `manualMode` is set back to `false` (AUTO resumes)

---

## Web Dashboard Overview

The dashboard is a standalone `index.html` file (not served from the ESP32). It:

* Displays:

  * ESP32 base URL
  * Connection status
  * Mode (AUTO / MANUAL) with color badge
  * Target temperature
  * Current temperature & humidity
  * Relay states (CH1–CH4)
  * Last update time
* Provides:

  * ON/OFF buttons for CH1, CH2, CH3, CH4

### Connecting the Dashboard

1. **Find ESP32 IP**

   * Open Serial Monitor or check the LCD:

     * It will print something like: `WiFi OK: 192.168.x.x`

2. **Edit `index.html`**

   * Find:

     ```js
     const ESP_BASE = "http://192.168.1.50";
     ```

   * Change to the actual ESP32 IP, e.g.:

     ```js
     const ESP_BASE = "http://192.168.184.87";
     ```

3. **Serve the HTML file**

   * Option A: VSCode + “Live Server” extension

   * Option B: Python simple HTTP server:

     ```bash
     python -m http.server 8000
     ```

   * Then open in browser:
     `http://localhost:8000/index.html`

4. **Network requirement**

   * Your PC/phone that opens the dashboard **must be on the same network** as the ESP32 (e.g. same phone hotspot / WiFi).

---

## REST API Reference

### `GET /status`

Returns current system status as JSON.

**Example response:**

```json
{
  "mode": "AUTO",
  "manual_remaining_s": 0,
  "target_temp": 26.0,
  "temperature": 26.5,
  "humidity": 60.0,
  "relay": {
    "ch1": false,
    "ch2": false,
    "ch3": true,
    "ch4": true
  }
}
```

* `mode` — `"AUTO"` or `"MANUAL"`
* `manual_remaining_s` — remaining manual-mode timeout in seconds
* `target_temp` — configured target temperature (°C)
* `temperature` — last measured temperature (°C) or `null` if not available
* `humidity` — last measured relative humidity (%) or `null`
* `relay.ch1..ch4` — `true` if relay is ON, `false` if OFF

### `GET /relay?ch=X&state=Y`

Control a single relay channel.

* `ch` — relay channel number (1–4)
* `state` — `0` = OFF, nonzero = ON

**Example:**

* Turn heater (CH2) ON:

  ```
  GET /relay?ch=2&state=1
  ```

* Turn Fan 1 (CH3) OFF:

  ```
  GET /relay?ch=3&state=0
  ```

**Response:**

Returns the same JSON structure as `/status`, representing the *updated* state.

> Any `/relay` call switches system to **MANUAL mode** and updates the manual timeout.

---

## Getting Started

1. **Clone this repository**

   ```bash
   git clone https://github.com/<your-username>/<your-repo-name>.git
   cd <your-repo-name>
   ```

2. **Install Arduino requirements**

   * Install **ESP32 board support** (via Boards Manager).
   * Install libraries:

     * `DHT sensor library for ESPx` / `DHTesp`
     * `LiquidCrystal_I2C`
   * Select the correct ESP32 board in Arduino IDE.

3. **Open the firmware sketch**

   * Open the `.ino` file in this repo.
   * Adjust:

     * `ssid` and `password`
     * Relay pins if your wiring is different

4. **Upload the sketch**

   * Connect ESP32 via USB.
   * Compile & upload from Arduino IDE / PlatformIO.
   * Open Serial Monitor @ 115200 to see logs.
   * Note the printed IP address after WiFi connects.

5. **Run the web dashboard**

   * Edit `index.html` to set the correct `ESP_BASE`.
   * Serve it locally and open in browser.
   * You should see up-to-date temperature, mode, and relay states.

---

## Troubleshooting

### ESP32 keeps rebooting / weird logs (`invalid header`, etc.)

* Make sure the sketch is correctly uploaded.
* Double-check board settings (correct ESP32 type, flash size, etc.).
* Try a different USB cable and port.

### WiFi connection is unstable / fails at first

* Sometimes ESP32 may take a few tries to connect to the hotspot.
* Check Serial Monitor for logs during WiFi connection.
* Ensure SSID/password are correct.
* Keep phone hotspot / router close to the ESP32.

### LCD 16×2 shows nothing

* Verify I2C wiring: SDA → GPIO 21, SCL → GPIO 22, GND/VCC correct.
* Check the I2C address:

  * Default is `0x27`, but some modules use `0x3F`.
  * Use an I2C scanner sketch to confirm.
* Adjust `LiquidCrystal_I2C lcd(0x27, 16, 2);` if needed.

### Relays not switching as expected

* Confirm relay is **active LOW**:

  * If `LOW = ON` and `HIGH = OFF`, the firmware constants are correct.
  * If your module behaves opposite, invert:

    * `const bool RELAY_ON = HIGH;`
    * `const bool RELAY_OFF = LOW;`
* Check wiring:

  * IN pins connected to the correct GPIOs
  * GND of ESP32 and relay module must be common

### Web dashboard cannot connect to ESP (`Disconnected`)

* Make sure:

  * `ESP_BASE` in `index.html` matches the ESP32 IP.
  * PC/phone running the dashboard is on the same WiFi network.
* Check browser console (F12 → Console) for CORS or network errors.

---

## License

Add your preferred license here, for example:

```text
MIT License

Copyright (c) 2025 <Your Name>
...
```

---

## Credits

This project was built as a small learning / practical project around:

* Embedded systems with **ESP32**
* Basic **IoT** concepts (sensing, control, HTTP API)
* Simple **web dashboard** for monitoring & control

Feel free to fork, modify, and adapt it for your own room, rack, incubator, or any other temperature-controlled setup. 😊

```
::contentReference[oaicite:0]{index=0}
```
