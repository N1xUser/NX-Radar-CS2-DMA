# NX-Radar-CS2-DMA (ESP32-S3 + ST7789)

**Status:** Undetected (External Hardware + Kernel Read)  
**IMPORTANT:** The offsets have to be always updated, you can find it here:
 - [Offsets.hpp](https://github.com/a2x/cs2-dumper/blob/main/output/offsets.hpp)
 - [Client_dll.hpp](https://github.com/a2x/cs2-dumper/blob/main/output/client_dll.hpp)

---

## Disclaimer

**EDUCATIONAL PURPOSES ONLY.**
Playing like an idiot will result in account bans (VAC/Game Ban). Since the software is undetected, the only way to get banned is by playing like a psycho and shooting through smokes all the time. Manual gameplay review is the only real threat, so just play like a 'very good player' and don't tell your team you have a 'little' help."

---
![MVIMG_20260105_195442](https://github.com/user-attachments/assets/4df067fb-2d90-4a87-aadd-6af3d97354d8)

## Hardware Requirements

*   **Microcontroller:** ESP32-S3 (DevKitC or similar) running circuitpython.
*   **Display:** ST7789 1.3" or 1.54" IPS LCD (240x240 resolution).
    *   *Note: Code is configured for Version 1.1 displays.*
*   **Connection:** USB-C Data Cable (for serial communication).
*   **Wiring:**
    *   **CLK (SCL):** GPIO 12
    *   **MOSI (SDA):** GPIO 11
    *   **RES (Reset):** GPIO 9
    *   **DC (Data/Command):** GPIO 8
    *   **CS (Chip Select):** GPIO 10

---

## Project Structure

```text
CS2-Hardware-Radar/
├── Driver/             # Kernel Driver (NXWire)
│   └── main.cpp        # Driver Entry & Memory Operations
├── Client/             # User-Mode Application
│   ├── main.cpp        # Logic to read memory & send to Serial
│   ├── client_dll.hpp  # Generated Schema
│   └── offsets.hpp     # Generated Offsets
└── Firmware/           # ESP32 CircuitPython Code
    ├── code.py         # Rendering Logic
    └── lib/
        ├── adafruit_display_text/  # Library to render text
        └── adafruit_st7789.mpy     # Library to render the graphs 
```

---

## Installation & Setup

### Part 1: The Kernel Driver (`NXWire`)

The driver handles the `MmCopyVirtualMemory` calls to read game memory from the kernel level, bypassing user-mode anti-cheat hooks.

1.  **Prerequisites:**
    *   Visual Studio 2022 with **Desktop development with C++**.
    *   **Windows Driver Kit (WDK)** installed.
2.  **Build:**
    *   Open the Driver project properties.
    *   Set configuration to **Release / x64**.
    *   Build the solution to generate `NXWire.sys`.
3.  **Loading:**
    *   This step depends, I sign this driver with a leaked chinese certificate that I found long time ago, if you don't have one, or you just use kdmapper, load it with that and you will be fine.

<img width="1920" height="1080" alt="screenshot" src="https://github.com/user-attachments/assets/e2f13e2d-d1ce-4fdf-82f6-243bc89e5b7f" />

### Part 2: The ESP32 Firmware

1.  **Install CircuitPython:**
    *   Put your ESP32-S3 into bootloader mode.
    *   Flash the latest **CircuitPython 9.x** `.uf2` for your specific board.
2.  **Install Libraries:**
    *   Download the [Adafruit CircuitPython Bundle](https://circuitpython.org/libraries).
    *   Copy the following folders/files to the `lib` folder on your `CIRCUITPY` drive:
        *   `adafruit_display_text/`
        *   `adafruit_st7789.mpy`
3.  **Deploy Code:**
    *   Copy the provided python script to the root of the drive and name it `code.py`.
    *   **Config:** If your screen colors are inverted or the screen is mirrored, adjust `ROTATION` or the initialization arguments in `code.py`.

### Part 3: The C++ Client

1.  **Update Offsets:**
    *   CS2 updates frequently. You must generate new offsets using a tool like [a2x/cs2-dumper](https://github.com/a2x/cs2-dumper), or download it on the page I put on the top.
    *   Place `client_dll.hpp` and `offsets.hpp` in the Client source folder.
2.  **Build:**
    *   Open the C++ Client project in Visual Studio.
    *   Set configuration to **Release / x64**.
    *   Build the executable.

---

## How to Run

1.  **Connect the Hardware:** Plug in the ESP32-S3 via USB. The screen should show:
    > "WAITING FOR PROGRAM..."
2.  **Load the Driver:**.
3.  **Start CS2:** Launch the game and wait until you are in a match.
4.  **Run the Client:**
    *   Run the compiled `NXConnect.exe` as **Administrator**.
    *   It will ask for the COM Port (e.g., if ESP32 is on COM3, type `3`).
5.  **Play:**
    *   The "Waiting" screen on the ESP32 will disappear once data is received.
    *   Enemies appear as **Red Dots**.
    *   You are the **Green Dot** (Center).
    *   **Spectator Mode:** If you die, the radar automatically adjusts to the POV of the player you are spectating, so a little help for the rest when you are dead.
    *   **Bomb:** When planted, an **'A'** or **'B'** indicator appears at the top of the radar, this is not 100% accurate, so on a couple of maps B = A and A = B.
---
<div align="center">
 
![VID_20260105_202137-ezgif com-resize](https://github.com/user-attachments/assets/dec5144f-afa3-4ad0-ae46-52b20dbeba67)

[Video Demo / Streamable](https://streamable.com/42xahw)
[Video Demo / YouTube](https://www.youtube.com/watch?v=p7mJm7JKATg)

</div>


---


## Configuration

### Radar Scale (Zoom)
To change how much of the map is visible, edit `code.py`:
```python
# Lower = Zoom In | Higher = Zoom Out
scale = (WIDTH / 2) / 2100.0 
```

### Player Position (Origin)
To move the player dot (to see more in front or behind), edit `code.py`:
```python
# +50 moves player down (see more in front)
RADAR_CY = RADAR_Y_START + (RADAR_HEIGHT // 2) + 50 
```

---

## Troubleshooting

*   **Screen is Black:**
    *   Check wiring (GPIO 12/11/9/8/10).
    *   Ensure CircuitPython libraries are in `lib/`.
*   **Windows crashes:**
    *   The `offsets` might be outdated. Update them.
*   **Enemies Not Appearing:**
    *   Update the `offsets and client_dll`

---
## Hardware (Mandatory)
*   **ESP32 S3**
    *   Aliexpress, like $7.
    *   [ESP32](https://es.aliexpress.com/item/1005009026897112.html)
*   **SPI 240x240 ST7789**
    *   Aliexpress, like $4.
    *   [Screen](https://es.aliexpress.com/item/1005008625277253.html)

## Hardware (Optional)
*   **Set 24AWG Cables, 8CM 120 u**
    *   Aliexpress, like $2
    *   [Cables](https://es.aliexpress.com/item/1005008194967488.html)
*   **Set Dupont Line, 10CM, 120 u**
    *   Aliexpress, like $2.7
    *   [Kit](https://es.aliexpress.com/item/1005007298861842.html)
*   **PCB Set, mixed, 5 u**
    *   Aliexpress, like $2.4
    *   [PCB](https://es.aliexpress.com/item/1005006467195096.html)
*   **1x40Pin 2,54mm, 10 Uds**
    *   Aliexpress, like $1.8
    *   [Connectors](https://es.aliexpress.com/item/1005007235591794.html)
