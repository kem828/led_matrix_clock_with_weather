# led_matrix_clock_with_weather

Clock with weather display for Raspberry Pi (tested on 3B) driving a 128x64 LED matrix panel.

Built on [hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix).

![Clock photo](https://github.com/kem828/led_matrix_clock_with_weather/blob/main/led_clock.jpg?raw=true)
Wide Mode
![Wide Mode photo](https://github.com/kem828/led_matrix_clock_with_weather/blob/main/wide_clock.jpg?raw=true)
All icons by [maxhollingsheadart](https://www.instagram.com/maxhollingsheadart)

---

## Hardware

- Raspberry Pi 3B (or similar)
- 128x64 HUB75 LED matrix panel
- Optional: second panel chained for wide mode (256x64 or 128x128)

---

## Dependencies

```bash
sudo apt install build-essential git libcurl4-openssl-dev nlohmann-json-dev
```

Build [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) per its instructions. The library directory should be a sibling of this project directory.

Get lodepng:
```bash
cd ./lodepng
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.h
curl -O https://raw.githubusercontent.com/lvandeve/lodepng/master/lodepng.cpp
```

Edit the Makefile to point to your rpi-rgb-led-matrix location if needed.

---

## Configuration

Edit `config.json` before running:

```json
{
  "api_key": "",
  "lat": "28.5",
  "lon": "-81.4",
  "units": "imperial",
  "day_brightness": 100,
  "night_brightness": 30,
  "night_start": 18,
  "night_end": 6,
  "clock_only": false,
  "clock_font": "classic"
}
```

- `api_key` — OpenWeatherMap API key (optional; falls back to Open-Meteo if empty)
- `lat` / `lon` — your location
- `units` — `"imperial"` or `"metric"`
- `day_brightness` / `night_brightness` — 1–100
- `night_start` / `night_end` — 24h hour values for the night brightness window
- `clock_only` — show only the clock, no weather panel
- `clock_font` — see table below

### Clock fonts

All fonts use programmatic 7-segment rendering. Each has two size presets: a smaller one for standard mode (128x64, clock shares the panel with weather) and a larger one for wide and clock-only modes.

| Name | Character | Standard (w×h×t) | Wide/clock-only (w×h×t) |
|---|---|---|---|
| `classic` | Proportioned segments, moderate thickness | 10×18×2 | 13×27×3 |
| `bold` | Thicker strokes, slightly wider | 11×19×3 | 16×36×4 |
| `slim` | Thin single-pixel strokes, tall aspect | 8×17×1 | 11×37×1 |
| `tall` | Narrow and very tall | 7×20×2 | 9×38×2 |
| `retro` | Wide and squat, thick strokes | 14×19×3 | 18×28×4 |

Dimensions are segment width × height × stroke thickness in pixels. The gap between digits is 1px in standard mode and 2px in wide/clock-only mode.

### Widget layouts

The layout for each display mode is also configurable in `config.json` under `"layouts"`. Each entry places a named module at an (x, y) pixel position. Available modules:

| Module | Size | Description |
|---|---|---|
| `weather_icon` | 32x32 | Current conditions icon |
| `temperature` | variable | Current temperature (x: -1 to auto-center) |
| `weather_desc` | variable | Conditions text |
| `day_date` | variable | Day of week + date |
| `moon_phase` | 16x16 | Current moon phase rendered procedurally |
| `forecast` | 32x30 | 3-day forecast with icons and high/low temps |
| `sunrise_sunset` | 16x32 | Today's sunrise and sunset times |

---

## Building

```bash
make
```

---

## Running

```bash
sudo ./clock
```

Standard rpi-rgb-led-matrix flags are passed through:

```bash
# Single 128x64 panel
sudo ./clock --led-rows=64 --led-cols=128 --led-gpio-slowdown=2

# Two 128x64 panels side by side (wide horizontal mode)
sudo ./clock --led-rows=64 --led-cols=128 --led-chain=2 --led-gpio-slowdown=2

# Two 128x64 panels stacked (wide vertical mode)
sudo ./clock --led-rows=64 --led-cols=128 --led-parallel=2 --led-gpio-slowdown=2
```

Custom flags (override config.json):

```bash
--day-brightness=N
--night-brightness=N
--night-start=H
--night-end=H
--clock-only
```

The display mode (standard / wide horizontal / wide vertical) is detected automatically from the matrix dimensions at startup.

For hardware wiring issues, see the [rpi-rgb-led-matrix documentation](https://github.com/hzeller/rpi-rgb-led-matrix).

---

## Weather data

- **Open-Meteo** (no key required) is used for temperature, current conditions, 3-day forecast, and sunrise/sunset times.
- **OpenWeatherMap** (free API key) is optional and used for the weather description when configured. Falls back to Open-Meteo if unconfigured or unreachable.
- Weather updates every 15 minutes. If a fetch fails, it retries after 60 seconds. The clock continues running with the last known data if the network is unavailable.

---



keinan@keinanmarks.com
