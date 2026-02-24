# ESP32-S3 ADS-B Radar Display

A high-performance, authentic PPI-style radar display for the [Waveshare ESP32-S3-Knob-Touch-LCD-1.8](https://www.waveshare.com/esp32-s3-knob-touch-lcd-1.8.htm). It pulls live aircraft data from a local dump1090/PiAware receiver and renders it with smooth, non-blocking animations.

![Radar Screenshot](https://www.waveshare.com/w/upload/thumb/d/d0/ESP32-S3-Knob-Touch-LCD-1.8-1.jpg/600px-ESP32-S3-Knob-Touch-LCD-1.8-1.jpg)

## Features

- **Smooth PPI Sweep:** 1.0° steps at 20ms intervals for a silky-smooth radar arm.
- **Multi-Core Architecture:** WiFi fetching runs on Core 0, while rendering and UI run on Core 1 — no network stutters.
- **Precise Rendering:** Blips are "painted" only when the sweep arm crosses their bearing.
- **Interactive Details:** Touch any aircraft to see its callsign, altitude, speed, and heading in a centered detail box.
- **Rotary Zoom:** Use the knob to adjust range from 5 nm to 250 nm.
- **AMOLED Optimization:** Deep black backgrounds and vibrant "radar green" palette.

## Project Structure

```
├── src/
│   ├── main.cpp          # Core logic (multi-core, PPI rendering, WiFi)
│   └── waveshare_init.h  # AMOLED driver initialization
├── include/
│   ├── config.h.example  # Template for your local setup
│   └── pins.h            # GPIO definitions
└── platformio.ini        # Build dependencies (GFX, ArduinoJson)
```

## Setup Instructions

1. **Clone the repo.**
2. **Configure:** Copy `include/config.h.example` to `include/config.h` and enter your WiFi credentials and your Pi's IP address.
3. **Build & Flash:**
   ```bash
   pio run -t upload
   ```

## Hardware Notes

- **USB Cable:** If programming fails, flip your USB-C cable 180° — the board's data lines are orientation-sensitive.
- **Knob:** This is a dual-switch mechanical encoder, not a quadrature one. The custom polling logic is in `main.cpp`.

## License

MIT
