# [Current private kernel project](https://nx-radar.duckdns.org/)


# NX-Radar-CS2-DMA (ESP32-S3 + ST7789)

# How to Build a Simple DMA Radar with Only an ESP32 + Cheap Screen

**The core idea:** Read game memory from kernel space → Send data over USB → Draw on a $4 display.  
No expensive FPGA DMA hardware. No PCIe cards. Just an ESP32, a small screen, and understanding how Windows memory works.

**IMPORTANT:** Game offsets change every update. Get fresh ones here:
- [Offsets.hpp](https://github.com/a2x/cs2-dumper/blob/main/output/offsets.hpp)
- [Client_dll.hpp](https://github.com/a2x/cs2-dumper/blob/main/output/client_dll.hpp)

---

## Disclaimer

**100% Educational – Teaching Hardware-Assisted Memory Reading**

This project demonstrates:
- How kernel memory reads bypass user-mode restrictions
- How to stream game data over USB to an external device
- How to render positions on a cheap SPI display

The technique is academically interesting. Don't be stupid with it.

---

## How Is This Even Possible? (The Short Answer)

Most game anti-cheats run in **user mode** (Ring 3). They hook Windows APIs like `ReadProcessMemory`. But they **cannot** block a kernel driver (Ring 0) from calling `MmCopyVirtualMemory` — that would break the OS.

So the chain is:

**Educational Focus:** External hardware-based radar using kernel read operations & serial display rendering.  
---

## Disclaimer

**FOR EDUCATIONAL PURPOSES ONLY.**  
This project demonstrates:
- Kernel → User → Hardware data flow
- External process memory reading via `MmCopyVirtualMemory`
- Real-time serial communication with embedded devices
- 2D map projection & rendering on a microcontroller

Behavior that is obviously inhuman (e.g., shooting through smokes consistently) can still lead to manual review. The educational value is in *understanding* the chain, not evading detection.

---
![MVIMG_20260105_195442](https://github.com/user-attachments/assets/4df067fb-2d90-4a87-aadd-6af3d97354d8)

## How the System Works (Educational Breakdown)

```
Game Memory (CS2)
       ↓
Kernel Driver (MmCopyVirtualMemory)
       ↓
User-Mode Client (Reads player positions)
       ↓
Serial (USB) to ESP32-S3
       ↓
ST7789 Display (Radar rendering)
```

1. **Kernel Driver** – Runs at Ring 0, bypasses user-mode anti-cheat hooks. Uses Windows `MmCopyVirtualMemory` to read CS2’s process memory safely from kernel space.
2. **User Client** – Reads game data (local player, enemies, bomb state) using offsets from `cs2-dumper`. Converts world coordinates to radar coordinates.
3. **Serial Handshake** – Client sends `RADAR_INIT` over COM port → ESP32 replies with `RADAR_ACK` → data stream starts.
4. **ESP32 Firmware** – Receives player/bomb data over USB serial, projects onto 240x240 screen, draws dots/indicators.
5. **Display** – ST7789 SPI screen shows:
   - Green dot = you (or spectated player)
   - Red dots = enemies
   - A/B = bomb plant indicator (not 100% map-accurate)

---

## Hardware Requirements

*   **Microcontroller:** ESP32-S3 (DevKitC or similar) running CircuitPython.
*   **Display:** ST7789 1.3" or 1.54" IPS LCD (240x240).
    - Code configured for Version 1.1 displays.
*   **Connection:** USB-C data cable (serial communication).
*   **Wiring (educational – shows SPI protocol):**
    - CLK (SCL) → GPIO 12
    - MOSI (SDA) → GPIO 11
    - RES (Reset) → GPIO 9
    - DC (Data/Command) → GPIO 8
    - CS (Chip Select) → GPIO 10

---

## Project Structure

```text
CS2-Hardware-Radar/
├── NX - Driver/        # Kernel driver – demonstrates MmCopyVirtualMemory
│   ├── NXConnect [Signed]/     # Signed driver (loads if certificate trusted)
│   └── NXConnect [Unsigned]/   # Unsigned driver (requires manual mapping)
├── NXBase/             # User-mode app – reads memory, sends to serial
│   └── src/            # Coordinate conversion, handshake logic, offsets
└── NX - DMA [ESP32S3]/ # ESP32 Firmwares
    ├── NXRadar [Arduino]/        # Native C++ Arduino firmware (High FPS)
    └── NXRadar [Circuitpython]/  # CircuitPython firmware (Easy to modify)
        ├── boot.py
        ├── code.py     # Serial receive, rendering, projection math
        └── lib/        # Adafruit libraries
```

---

## Installation & Setup (Educational Walkthrough)

### Part 1: Kernel Driver – Why Ring 0?

User-mode anti-cheats hook Windows API calls like `ReadProcessMemory`. By reading memory from a kernel driver using `MmCopyVirtualMemory`, we bypass those hooks completely.

1. **Prerequisites:**
   - Visual Studio 2022 + **Desktop development with C++**
   - **Windows Driver Kit (WDK)** – matches your Windows build
2. **Build:** Release / x64 → generates `NXWire.sys`
3. **Loading (educational note):**
   - Signed driver → loads normally if certificate is trusted
   - Unsigned → requires manual mapping (e.g., kdmapper) – this is fragile and purely for learning

> *What you learn here:* How kernel memory reading works, why drivers have higher privilege, and why anti-cheats detect unsigned mappings.

<img width="1920" height="1080" alt="screenshot" src="https://github.com/user-attachments/assets/e2f13e2d-d1ce-4fdf-82f6-243bc89e5b7f" />

### Part 2: ESP32 Firmware – Embedded Serial & Graphics

The ESP32 acts as a **dumb terminal** – it receives data over USB and renders it. You can choose either the **CircuitPython** or **Arduino** (Native) firmware. Both are fully compatible with the same NXBase client thanks to a unified handshake protocol.

#### Option A: CircuitPython (Easier to modify)
1. Flash **CircuitPython 9.x** `.uf2` to your ESP32-S3 via bootloader mode.
2. Copy `boot.py`, `code.py` and the `lib/` folder from `NX - DMA [ESP32S3]/NXRadar [Circuitpython]/` to your `CIRCUITPY` drive.
3. The script will automatically run on boot, handling the serial read, coordinate math, and rendering.

#### Option B: Arduino / C++ (Higher performance, 60fps+)
1. Open `NX - DMA [ESP32S3]/NXRadar [Arduino]/NXRadar.ino` in the Arduino IDE.
2. Install the **LovyanGFX** library via the Arduino Library Manager.
3. Select **ESP32S3 Dev Module** as your board.
4. Set **USB CDC On Boot: Enabled** and **USB Mode: Hardware CDC and JTAG** in the Tools menu.
5. Compile and upload to your ESP32-S3.

> *What you learn here:* Real-time embedded rendering, SPI communication, coordinate projection.

### Part 3: C++ Client – Memory Reading & Serial Protocol

1. Update offsets using [cs2-dumper](https://github.com/a2x/cs2-dumper)
2. Build **Release / x64**
3. Run as Administrator (required for driver communication)

> *What you learn here:* How to read external process memory, convert game coordinates to screen space, and implement a serial handshake.

---

## How It Works (Step-by-Step Execution)

1. **Connect ESP32** → USB → Shows: `"WAITING FOR PROGRAM..."`
2. **Load Kernel Driver** → Grants read access to CS2 memory
3. **Launch CS2** → Enter a match
4. **Run Client** (Admin) → 
   - Scans COM ports 1–20
   - Sends `RADAR_INIT\n`
   - Waits for `RADAR_ACK\n` (2 sec timeout)
   - On success → streams player data
5. **ESP32 renders**:
   - World coordinates → screen pixels
   - Center is always you (or spectated player)
   - Rotation applied to match in-game forward direction

### Handshake Protocol (Educational)

| Step | Direction | Data |
|------|-----------|------|
| 1 | ESP32 → PC | `RADAR_READY\n` (Beacon sent every 500ms until connected) |
| 2 | PC → ESP32 | `RADAR_INIT\n` |
| 3 | ESP32 → PC | `RADAR_ACK\n` |
| 4 | PC → ESP32 | Text-based player/bomb data (`p,x,y,ang;e,...`) |

This ensures the correct COM port is auto-detected seamlessly, regardless of which firmware (CircuitPython or Arduino) is loaded, and confirms the display is ready.

---

<div align="center">

![VID_20260105_202137-ezgif com-resize](https://github.com/user-attachments/assets/dec5144f-afa3-4ad0-ae46-52b20dbeba67)

[Video Demo / Streamable](https://streamable.com/42xahw)  
[Video Demo / YouTube]()

</div>

---

## Configuration (Understanding the Math)

### Radar Scale (Zoom)
```python
# Lower = Zoom In | Higher = Zoom Out
scale = (WIDTH / 2) / 2100.0 
```
How it works: world units → pixels. `2100` is ~half the visible radius in game units.

### Player Position Offset (View Adjustment)
```python
# +50 moves player down (see more in front)
RADAR_CY = RADAR_Y_START + (RADAR_HEIGHT // 2) + 50 
```
This shifts your dot, effectively changing what you see ahead vs. behind.

---

## Troubleshooting (Educational)

| Symptom | Likely Cause | What You Learn |
|---------|--------------|----------------|
| Black screen | Wrong SPI wiring or missing libs | Hardware debugging |
| Windows crash | Outdated offsets | Memory structures change with game updates |
| No enemies | `client_dll.hpp` mismatch | Game schema changes |
| COM port not found | Wrong baud or handshake mismatch | Serial protocol debugging |

### Debug Logging
Create `out.txt` on Desktop → client logs:
- COM port scan results
- Handshake success/failure
- Read errors

Useful for understanding the auto-detection flow.

---

## Hardware (Mandatory – Educational)

| Component | Cost | Why it works |
|-----------|------|---------------|
| ESP32-S3 | ~$7 | USB native, fast enough for real-time rendering |
| ST7789 240x240 | ~$4 | SPI display – simple protocol to learn |

- [ESP32 on AliExpress](https://es.aliexpress.com/item/1005009026897112.html)
- [Screen on AliExpress](https://es.aliexpress.com/item/1005008625277253.html)

## Hardware (Optional)

- 24AWG cables, 8cm
- Dupont lines, 10cm
- Mixed PCB set
- 2.54mm pin headers

> *Why optional:* The educational core is software + wiring. Breadboards work fine for learning.

---

## What You Learn From This Project

- Kernel vs. user mode memory access
- Windows driver development basics (`MmCopyVirtualMemory`)
- Reading external game memory using offsets
- Coordinate transformation (world → screen)
- Serial communication & handshake protocols
- Embedded rendering (CircuitPython + SPI display)
- Real-time data streaming over USB

This is not a "cheat" – it’s a **hardware-assisted memory visualization tool** built for learning OS, embedded, and game hacking countermeasures.

---

**License & Ethics**  
Use this code only on systems you own, for educational research. Reverse engineering game memory violates almost all game EULAs. Understand the technique, don't abuse it.
