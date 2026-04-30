/**
 * OeffiClient – Example Sketch
 *
 * Connects to WiFi and fetches real-time departure data from the
 * Wiener Linien OGD Realtime API using the OeffiClient library.
 *
 * Board:    ESP32 (any variant)
 * Framework: Arduino / PlatformIO
 *
 * Required library:
 *   - ArduinoJson  ^6.21.4  (bblanchon/ArduinoJson)
 *
 * Wiring:   none – WiFi only
 */

#include <Arduino.h>
#include <WiFi.h>
#include "OeffiClient.h"

// ── WiFi credentials ──────────────────────────────────────────────────────────
static constexpr const char* WIFI_SSID = "YourSSID";
static constexpr const char* WIFI_PASS = "YourPassword";

// ── Refresh interval ──────────────────────────────────────────────────────────
static constexpr unsigned long REFRESH_INTERVAL_MS = 60000; // 60 s

// ── Global client instance ────────────────────────────────────────────────────
OeffiClient oeffi;

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: print one direction
// ─────────────────────────────────────────────────────────────────────────────
static void printDirection(const char* label,
                            const DirectionDepartures& dir,
                            int maxEntries = 3) {
    Serial.printf("  %s (towards: %s)\n", label, dir.towards.c_str());

    if (dir.departures.empty()) {
        Serial.println("    – no departures –");
        return;
    }

    int shown = 0;
    for (const Departure& dep : dir.departures) {
        if (shown >= maxEntries) break;
        Serial.printf("    %2d min  |  real: %-25s  |  barrier-free: %s",
                      dep.countdown,
                      dep.timeReal.isEmpty() ? dep.timePlanned.c_str()
                                             : dep.timeReal.c_str(),
                      dep.barrierFree ? "yes" : "no");

        if (dep.hasDisruption()) {
            if (dep.disruptionMessage.length() > 0)
                Serial.printf("  ⚠  %s", dep.disruptionMessage.c_str());
            else
                Serial.print("  ⚠  traffic disruption");
        }

        Serial.println();
        shown++;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helper: print a full LineDepartures result
// ─────────────────────────────────────────────────────────────────────────────
static void printLineDepartures(const LineDepartures& ld) {
    Serial.printf("\n══ Line %-6s  |  Stop: %s ══\n",
                  ld.lineName.c_str(),
                  ld.stopTitle.c_str());

    if (!ld.valid) {
        Serial.printf("  ERROR: %s\n", ld.errorMsg.c_str());
        return;
    }

    printDirection("→ H (Hin / towards city)",  ld.directionH);
    printDirection("← R (Retour / outbound)",   ld.directionR);
}

// ─────────────────────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[OeffiClient] Booting…");

    // ── Connect WiFi ──
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[OeffiClient] Connecting to WiFi");

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }

    Serial.printf("\n[OeffiClient] Connected – IP: %s\n",
                  WiFi.localIP().toString().c_str());

    oeffi.begin();
}

// ─────────────────────────────────────────────────────────────────────────────
//  loop()
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    static unsigned long lastFetch = 0;

    unsigned long now = millis();
    if (now - lastFetch < REFRESH_INTERVAL_MS && lastFetch != 0) return;
    lastFetch = now;

    Serial.println("\n[OeffiClient] Fetching departures…");

    // ── Example 1: Badner Bahn (WLB) ──────────────────────────────────────
    // Stop IDs: 5942 (inbound / Schedifkaplatz), 5900 (outbound)
    {
        LineDepartures wlb = oeffi.getDepartures({5942, 5900}, "WLB");
        printLineDepartures(wlb);

        // Quick-access helpers:
        Departure nextIn  = wlb.directionH.next();
        Departure nextOut = wlb.directionR.next();
        Serial.printf("  → next WLB stadteinwärts: %d min\n", nextIn.countdown);
        Serial.printf("  ← next WLB stadtauswärts: %d min\n", nextOut.countdown);
    }

    // ── Example 2: Tram 62 ────────────────────────────────────────────────
    // Stop IDs: 1746 (inbound), 1720 (outbound)
    {
        LineDepartures t62 = oeffi.getDepartures({1746, 1720}, "62");
        printLineDepartures(t62);
    }

    // ── Example 3: Multiple lines, single HTTP request ────────────────────
    // Fetch WLB and 62 together from a shared set of stops
    {
        auto multi = oeffi.getDeparturesMultiLine(
            {5942, 5900, 1746, 1720},   // all stop IDs
            {"WLB", "62"}               // lines of interest
        );

        Serial.println("\n── Multi-line result ──");
        for (const auto& ld : multi) {
            Serial.printf("  %-6s  → %d min  ← %d min\n",
                          ld.lineName.c_str(),
                          ld.directionH.next().countdown,
                          ld.directionR.next().countdown);
        }
    }

    // ── Example 4: Any other line / stop ─────────────────────────────────
    // Simply swap stop IDs and line name – no code changes required.
    // LineDepartures u3 = oeffi.getDepartures({4905, 4912}, "U3");
    // printLineDepartures(u3);
}
