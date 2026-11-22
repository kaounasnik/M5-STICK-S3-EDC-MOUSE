# M5-STICK-S3-EDC-MOUSE
Firmware that uses the m5 stick s3 as a bluetooth mouse.

![M5StickS3 EDC Mouse Icon](M5STICKS3_EDC_MOUSE_ICON.svg)

A wireless Bluetooth Low Energy (BLE) HID mouse firmware for the **M5StickS3** using only its built-in **BMI270 6-axis IMU** and onboard buttons—no external GPIO modules required.

Place it flat on a desk, tilt to steer the cursor, and twist it like a rotary dial to scroll.

## Features

- **Zero External Hardware:** Runs entirely on the stock M5StickS3 (ESP32-S3 + BMI270 IMU).
- **Tilt-Steered Cursor:** Smooth 100 Hz acceleration-integrated pointer movement with deadzone filtering, friction damping, and Zero-Velocity Updates (ZUPT) to eliminate drift.
- **Desk-Dial Twist Scrolling:** Uses the Z-axis gyroscope as a notched rotary scroll wheel—twist clockwise or counter-clockwise flat on the desk to scroll.
- **Full Mouse Clicks:** Left-click (with click-and-drag support), right-click, and instant 1-second dual-button recalibration.
- **Custom Screen Media (Optional):** Display a custom `.png`, animated `.gif`, or auto-scaled `.svg` on the 135×240 IPS display—or build with no assets at all for a clean black screen.
- **Live Battery Overlay & BLE Sync:** Color-coded top-right battery percentage badge (green / yellow / red / cyan when charging) that also reports real battery percentage to your host OS over Bluetooth.
- **Auto-Dim Power Saving:** Turns off the backlight (and pauses GIF decoding) after 30 seconds of inactivity, waking instantly on any tilt, twist, or click.

## Controls

| Input | Action |
| :--- | :--- |
| **Tilt device** | Move cursor |
| **Twist clockwise / counter-clockwise** | Scroll wheel |
| **Front Button (`BtnA`)** | Left Click (hold for click-and-drag) |
| **Side Button (`BtnB`)** | Right Click |
| **Hold `BtnA` + `BtnB` (1 second)** | Recalibrate IMU zero-point |

## Getting Started

### Prerequisites

- **Hardware:** M5StickS3 + USB-C data cable
- **Software:** [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/) extension installed

### Build & Flash

1. Clone this repository:
   ```bash
   git clone [https://github.com/kaounasnik/M5-STICK-S3-EDC-MOUSE.git](https://github.com/kaounasnik/M5-STICK-S3-EDC-MOUSE.git)
   cd M5-STICK-S3-EDC-MOUSE
   ```
2. Open the folder in VS Code and wait for PlatformIO to finish initializing.
3. Connect your M5StickS3 over USB-C and click **PlatformIO: Upload (`→`)** in the bottom status bar.
4. Immediately set the M5StickS3 **flat on your desk** during the 1-second startup calibration.
5. Open Bluetooth settings on your PC and pair with **`M5StickS3 Mouse`**.

## Customizing the Screen

You can optionally drop any of these files into the `src/` folder:

- `src/screen.png` — Static image (135×240 portrait)
- `src/screen.gif` — Animated GIF (135×240 portrait; transparent pixels automatically render as pure black)
- `src/screen.svg` — Vector graphic (automatically cropped to artwork bounds, scaled, and centered below the battery pill)

Select the active mode at the top of `src/main.cpp`:

```cpp
// Choose screen type: "1" for .png, "2" for .gif, "3" for .svg
#define SCREEN_TYPE 1
```

All media files are optional—`embed_assets.py` automatically detects what is present before compiling. If the selected asset is missing from `src/`, the firmware still compiles cleanly and defaults to a black screen with the battery indicator.

## Tuning

All feel and power parameters live at the top of `src/main.cpp`:

| Constant | Default | What It Controls |
| :--- | :--- | :--- |
| `SENSITIVITY` | `45.0f` | Cursor speed |
| `DEADZONE` | `0.035f` | Minimum tilt/accel ignored as stillness |
| `FRICTION` | `0.92f` | Velocity decay per tick (lower = snappier stop) |
| `SCROLL_DEADZONE_DPS` | `25.0f` | Minimum twist speed (deg/s) before scrolling starts |
| `SCROLL_DEG_PER_NOTCH` | `12.0f` | Degrees of twist per scroll tick (lower = faster scroll) |
| `INVERT_SCROLL` | `false` | Reverses scroll direction |
| `SCREEN_BRIGHTNESS` | `128` | Active display brightness (`0`–`255`) |
| `IDLE_DIM_MS` | `30000` | Inactivity timeout (ms) before backlight turns off |
| `SVG_SCALE_FILL` | `0.92f` | Target fill ratio for `.svg` artwork |

## License

This project is licensed under the MIT License—see the [LICENSE](LICENSE) file for details.
