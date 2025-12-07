# Room Temperature Controller – ESP32 + DHT11 + LCD 16×2 + Web Dashboard

This repository contains a small IoT project for **monitoring and controlling room temperature** using:

- **ESP32** as the main controller & REST API server  
- **DHT11** sensor for temperature & humidity  
- **LCD 16×2 I2C** for local display  
- **4-channel relay** for controlling:
  - CH1 – empty / reserved for future use  
  - CH2 – PTC heater  
  - CH3 – Fan 1  
  - CH4 – Fan 2  
- A **separate web dashboard** (HTML + CSS + JavaScript) for monitoring and manual control

The system tries to keep the room at around **26 °C** using automatic control, but also allows **manual override** from the web UI. When the user stops interacting for a while, the system automatically returns to AUTO mode.

---

## Features

- 🌡️ **Automatic temperature control**
  - Target temperature: **26 °C**
  - Uses **hysteresis** (±0.8 °C) to avoid relay “chattering”
  - Too hot → fans ON, heater OFF  
  - Too cold → heater ON, fans OFF  
  - Comfortable range → all outputs OFF (energy saving)

- 📟 **On-device LCD display (16×2 I2C)**
  - Shows temperature & humidity
  - Shows mode: **AUTO / MAN**
  - Shows heater & fan states in compact format (e.g. `AUT H:0 F:12`)

- 🌐 **ESP32 REST API**
  - `GET /status` → JSON with sensor data, mode, and relay states
  - `GET /relay?ch=X&state=Y` → manual control for each relay channel

- 🕹 **Web Dashboard (standalone HTML file)**
  - Real-time monitoring with auto-refresh
  - Manual ON/OFF buttons for all 4 relay channels
  - Mode indicator (AUTO / MANUAL) + manual timeout countdown

- 👤 **User-priority manual mode**
  - Any manual command from the web switches the system into **MANUAL** mode
  - AUTO logic is temporarily disabled (user has full control)
  - After a configurable timeout with no user input, the system **returns to AUTO**

---

## System Overview

High-level data flow:

1. **DHT11** reads temperature & humidity → sends data to **ESP32**
2. **ESP32**:
   - Runs the automatic control logic
   - Drives the **relay board** (heater + fans)
   - Updates the **LCD 16×2** with current status
   - Exposes REST API (`/status`, `/relay`) over HTTP
3. **Web Dashboard**:
   - Runs in a regular browser (phone / laptop)
   - Periodically calls `/status` (polling)
   - Sends manual control commands via `/relay?ch=X&state=Y`
4. **User device** and **ESP32** must be connected to the **same WiFi network** (e.g. phone hotspot).

---

## Hardware

### Components

- 1 × ESP32 Dev Module  
- 1 × DHT11 temperature & humidity sensor  
- 1 × LCD 16×2 with I2C backpack (default address `0x27`)  
- 1 × 4-channel relay module (active LOW)  
- 1 × PTC heater (rated according to relay & supply)  
- 2 × DC fans (Fan 1 and Fan 2)  
- 1 × 5 V power supply for relay & loads  
- Jumper wires, breadboard / PCB, etc.

> ⚠️ **Safety note**  
> If you use mains-powered heater or high-voltage loads, please handle isolation, grounding, and protection properly. This repo focuses on control logic, not mains wiring best practices.

### ESP32 Pinout (default firmware)

- **DHT11**
  - Data → `GPIO 19`

- **Relays**
  - CH1 → `GPIO 0`   (empty / reserved – *boot strapping pin, see note below*)  
  - CH2 → `GPIO 14`  (PTC heater)  
  - CH3 → `GPIO 12`  (Fan 1)  
  - CH4 → `GPIO 13`  (Fan 2)  

- **LCD 16×2 I2C**
  - SDA → `GPIO 21`
  - SCL → `GPIO 22`
  - I2C address: `0x27` (change in code if yours is `0x3F` or others)

> ⚠️ **Boot note (GPIO 0)**  
> GPIO0 is a boot / strapping pin. If the relay + load on CH1 pulls this pin LOW during boot, ESP32 may fail to boot or enter download mode.  
> If you experience boot issues, move CH1 to a safer GPIO (e.g. 23 / 25 / 26 / 27) and update the pin definitions in the firmware.

---

## Software Stack

- **ESP32 firmware**
  - Arduino Core for ESP32
  - Libraries:
    - `WiFi.h`
    - `WebServer.h`
    - `DHTesp.h`
    - `LiquidCrystal_I2C.h`
  - Language: C/C++ (Arduino-style)

- **Web Dashboard**
  - HTML5 for structure  
  - CSS for layout and visual design (cards, gradient background, badges)  
  - Vanilla JavaScript using `fetch()` for API calls

You can develop and upload the firmware with:

- **Arduino IDE** (simplest), or  
- **PlatformIO** (VSCode extension), etc.

---

## Firmware Logic

### Modes: AUTO vs MANUAL

- **AUTO mode**
  - `manualMode == false`
  - Temperature-based control is active (`updateAutoControl()`)
  - Relays are set according to temperature relative to target + hysteresis

- **MANUAL mode**
  - Triggered whenever `/relay?ch=X&state=Y` is called
  - `manualMode == true`
  - Automatic control is *paused*: `updateAutoControl()` returns early
  - Each manual command updates `lastUserActionMillis`
  - When `MANUAL_TIMEOUT_MS` has passed without any new command, the firmware automatically switches back to AUTO

### Temperature Control

Configured constants:

```cpp
const float TARGET_TEMP = 26.0;   // °C
const float HYSTERESIS  = 0.8;    // °C
```

Effective zones:

- **Too hot** → `T > 26.8 °C`  
  - Heater (CH2) OFF  
  - Fan1 (CH3) ON  
  - Fan2 (CH4) ON  

- **Too cold** → `T < 25.2 °C`  
  - Heater (CH2) ON  
  - Fan1 (CH3) OFF  
  - Fan2 (CH4) OFF  

- **Comfort zone** → `25.2 °C ≤ T ≤ 26.8 °C`  
  - Heater OFF  
  - Fans OFF  

Relays are **active LOW**, and this is handled by:

```cpp
const bool RELAY_ON  = LOW;
const bool RELAY_OFF = HIGH;
```

### LCD Output

The LCD is updated periodically and usually shows:

- Line 1: `T:26.3C H:60%`  
- Line 2: `AUT H:0 F:12` or `MAN H:1 F:0`, etc.

- `AUT` / `MAN` — current mode  
- `H:0/1` — heater OFF/ON  
- `F:0/1/2/12` — fan combination:
  - `0` → both fans OFF  
  - `1` → Fan 1 ON only  
  - `2` → Fan 2 ON only  
  - `12` → both Fan 1 and Fan 2 ON

---

## REST API

### `GET /status`

Returns the full system state as JSON.

**Example:**

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

- `mode` — `"AUTO"` or `"MANUAL"`
- `manual_remaining_s` — remaining manual-mode timeout in seconds
- `target_temp` — configured target temperature (°C)
- `temperature` — last measured temperature (°C) or `null` if not available
- `humidity` — last measured relative humidity (%) or `null`
- `relay.ch1..ch4` — boolean, `true` = ON, `false` = OFF

### `GET /relay?ch=X&state=Y`

Changes one relay channel state and switches to MANUAL mode.

- `ch` — relay channel (1–4)  
- `state` — `0` = OFF, non-zero = ON

**Examples:**

- Turn heater (CH2) ON:

  ```text
  GET /relay?ch=2&state=1
  ```

- Turn Fan 1 (CH3) OFF:

  ```text
  GET /relay?ch=3&state=0
  ```

Response: same JSON format as `/status`, with the *updated* state.

> Any `/relay` request will:
> - Set `manualMode = true`
> - Reset the manual timeout counter
> - Return the current status JSON payload

---

## Web Dashboard

The dashboard is a standalone file (e.g. `index.html`) and **not** embedded in the ESP32. It:

- Shows:
  - ESP32 base URL
  - Connection status
  - Mode (AUTO/MANUAL)
  - Target temperature
  - Current temperature & humidity
  - Relay states for CH1–CH4
  - Last update time

- Provides:
  - ON/OFF buttons for each relay channel
  - Refresh button to manually re-poll `/status`
  - Auto-refresh interval (default: every 5 seconds)

### Setup Steps

1. **Flash the ESP32 firmware**
   - Configure your WiFi SSID & password in the code
   - Upload the `.ino` file to ESP32
   - Open Serial Monitor or check the LCD to get the ESP32 IP address (e.g. `192.168.184.87`)

2. **Edit the dashboard’s base URL**
   - Open `index.html`
   - Find:

     ```js
     const ESP_BASE = "http://192.168.1.50";
     ```

   - Replace with your ESP32 IP, for example:

     ```js
     const ESP_BASE = "http://192.168.184.87";
     ```

3. **Serve the HTML file**
   - Option 1: VSCode + “Live Server” extension  
   - Option 2: Simple Python HTTP server:

     ```bash
     python -m http.server 8000
     ```

   - Then open: `http://localhost:8000/index.html`

4. **Ensure same network**
   - Your PC/phone and the ESP32 must be on the **same WiFi network** (e.g. same hotspot).

---

## Getting Started (Quick)

1. Clone this repo:

   ```bash
   git clone https://github.com/<your-username>/<this-repo>.git
   cd <this-repo>
   ```

2. Open the **Arduino sketch** in Arduino IDE.
3. Install required libraries:
   - `DHTesp`  
   - `LiquidCrystal_I2C`  
   - ESP32 core (via Boards Manager)
4. Set your WiFi credentials in the `.ino` file.
5. Upload the firmware to ESP32.
6. Note the IP from Serial Monitor / LCD.
7. Edit `index.html` → set `ESP_BASE` → serve it locally.
8. Open the dashboard in browser and start testing.

---

## Known Issues & Notes

- **WiFi connection issues**  
  On some setups, the ESP32 may occasionally struggle to connect to WiFi (e.g. hotspot quirks). Rebooting the board and checking logs via Serial Monitor helps identify whether it’s stuck during WiFi initialization.

- **GPIO 0 as relay input**  
  If CH1 is connected to something that pulls GPIO0 low at boot, ESP32 may fail to boot. If that happens, move CH1 to another GPIO and update the code.

- **DHT11 accuracy**  
  DHT11 is a simple/cheap sensor with limited accuracy. If you need better precision or stability, consider using DHT22/SHT series and adjust the code accordingly.

---

## License

Add your license of choice here, for example:

```text
MIT License

Copyright (c) 2025 <Your Name>
```

---

## Credits

This project was created as a small practical experiment combining:

- Embedded systems with **ESP32**
- Basic **IoT** patterns (sensing, local control, HTTP API)
- A simple **web dashboard** for user-friendly monitoring & control

Feel free to fork, modify, and adapt it for your own room, rack, incubator, or any other DIY temperature control setup. 🙂
