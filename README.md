# OeffiClient – ESP32 Library for Wiener Linien Realtime API

A lightweight, PlatformIO-compatible C++ library for querying real-time departure data from the **Wiener Linien OGD Realtime API** on an ESP32.

---

## Features

| Feature | Detail |
|---|---|
| Generic stop/line query | Pass any stop ID list and any line name |
| Direction separation | Results split into `directionH` (Hin) and `directionR` (Retour) |
| Disruption mapping | Traffic disruption messages linked to departures |
| Multi-line fetch | Query several lines in one HTTP call |
| Barrier-free flag | Per-departure accessibility info |
| Real-time vs planned | Both timestamps available per departure |
| ArduinoJson v6 | Heap-based parsing, no stack overflow |

---

## Installation (PlatformIO)

### Automatic import
1. Add ArduinoJson and OeffiClient to your `platformio.ini`:

```ini
lib_deps =
    bblanchon/ArduinoJson @ ^6.21.4
    https://github.com/SebastianFH-png/OeffiClient.git
```

### Manual import
1. Copy the `src/OeffiClient.h` and `src/OeffiClient.cpp` files into your project's `src/` directory (or a `lib/OeffiClient/` folder).
2. Add ArduinoJson to your `platformio.ini`:

```ini
lib_deps =
    bblanchon/ArduinoJson @ ^6.21.4
```

---

## Quick Start

```cpp
#include <WiFi.h>
#include "OeffiClient.h"

OeffiClient oeffi;

void setup() {
    WiFi.begin("SSID", "PASSWORD");
    while (WiFi.status() != WL_CONNECTED) delay(500);
    oeffi.begin();
}

void loop() {
    // Badner Bahn: stop 5942 (inbound) + 5900 (outbound)
    LineDepartures wlb = oeffi.getDepartures({5942, 5900}, "WLB");

    if (wlb.valid) {
        Departure next = wlb.directionH.next();   // next departure towards city
        Serial.printf("WLB stadteinwärts: %d min\n", next.countdown);
    }

    // Tram 62
    LineDepartures t62 = oeffi.getDepartures({1746, 1720}, "62");
    Serial.printf("62 stadteinwärts: %d min\n", t62.directionH.next().countdown);

    delay(60000);
}
```

---

## API Reference

### `OeffiClient`

#### `bool begin()`
Checks WiFi connection. Returns `false` if not connected. Optional but recommended.

#### `LineDepartures getDepartures(vector<int> stopIds, String lineName)`
Fetches departures for the given stop IDs and filters to `lineName`.

#### `LineDepartures getDepartures(int stopId, String lineName)`
Single-stop convenience overload.

#### `vector<LineDepartures> getDeparturesMultiLine(vector<int> stopIds, vector<String> lineNames)`
One HTTP request, multiple lines returned. More efficient when stops overlap.

#### `void setHttpTimeout(int ms)`
Override default HTTP timeout (default: 8000 ms).

#### `const String& lastRawJson()`
Returns the raw JSON string of the last response, useful for debugging.

---

### `LineDepartures`

| Field | Type | Description |
|---|---|---|
| `lineName` | `String` | Requested line name |
| `stopId` | `int` | RBL stop ID (first matched) |
| `stopTitle` | `String` | Human-readable stop name |
| `directionH` | `DirectionDepartures` | "Hin" direction departures |
| `directionR` | `DirectionDepartures` | "Retour" direction departures |
| `valid` | `bool` | `false` if fetch or parse failed |
| `errorMsg` | `String` | Error description if `!valid` |

---

### `DirectionDepartures`

| Field | Type | Description |
|---|---|---|
| `direction` | `String` | `"H"` or `"R"` |
| `towards` | `String` | Representative destination label |
| `departures` | `vector<Departure>` | All departures, sorted as returned by API |
| `next()` | `Departure` | Shortcut to `departures[0]`, or empty if none |

---

### `Departure`

| Field | Type | Description |
|---|---|---|
| `countdown` | `int` | Minutes until departure (`-1` = unknown) |
| `timePlanned` | `String` | Scheduled time (ISO 8601) |
| `timeReal` | `String` | Real-time prediction (empty if unavailable) |
| `towards` | `String` | Final destination |
| `direction` | `String` | `"H"` or `"R"` |
| `barrierFree` | `bool` | Wheelchair accessible |
| `realtimeSupported` | `bool` | Real-time data available for this line |
| `trafficJam` | `bool` | Traffic disruption in approach |
| `disruptionMessage` | `String` | Disruption text, empty if none |
| `hasDisruption()` | `bool` | `true` if any disruption applies |

---

## Finding Stop IDs

Stop IDs (RBL numbers) can be found in the official CSV:
`wienerlinien-ogd-haltepunkte.csv` – the `StopID` column.

Common examples:

| Stop | Line | Direction | StopID |
|---|---|---|---|
| Schedifkaplatz | WLB | inbound | 5942 |
| Schedifkaplatz | WLB | outbound | 5900 |
| Wienerbergbrücke | 62 | inbound | 1746 |
| Wienerbergbrücke | 62 | outbound | 1720 |

---

## Memory & Performance Notes

- `JSON_DOC_SIZE` defaults to **32 KB** heap. Increase in `OeffiClient.h` if parsing fails on responses with many stops/departures.
- Each HTTP response is temporarily stored as a `String` before JSON parsing; on very heap-constrained boards, stream-parse directly if needed.
- The library uses `std::vector` — available on ESP32 Arduino Core without issues.
