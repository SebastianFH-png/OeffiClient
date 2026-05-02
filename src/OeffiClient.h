#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────
//  Data Structures
// ─────────────────────────────────────────────

/**
 * Represents a single departure at a stop.
 */
struct Departure {
    int countdown = -1; ///< Minutes until departure (-1 = unknown)
    String timePlanned; ///< Scheduled departure time (ISO 8601)
    String timeReal; ///< Real-time departure time (ISO 8601), empty if not available
    String towards; ///< Final destination name
    String direction; ///< API direction code: "H" (Hin) or "R" (Retour)
    bool barrierFree = false; ///< true if vehicle is wheelchair accessible
    bool realtimeSupported = false;
    bool trafficJam = false;
    String disruptionMessage; ///< Non-empty if a traffic disruption is linked

    /** Returns true if any disruption information is available. */
    bool hasDisruption() const {
        return trafficJam || disruptionMessage.length() > 0;
    }
};

/**
 * Collection of departures for one direction, identified by a direction code.
 */
struct DirectionDepartures {
    String direction; ///< "H" or "R"
    String towards; ///< Representative destination label
    std::vector<Departure> departures;

    /** Returns the next departure, or an empty Departure if none available. */
    Departure next() const {
        if (!departures.empty()) return departures[0];
        return Departure{};
    }
};

/**
 * All departures for a queried stop and line combination.
 */
struct LineDepartures {
    String lineName;
    int stopId = 0;
    String stopTitle;
    DirectionDepartures directionH; ///< "Hin" direction
    DirectionDepartures directionR; ///< "Retour" direction
    bool valid = false; ///< false if fetch/parse failed
    String errorMsg;
};

// ─────────────────────────────────────────────
//  OeffiClient
// ─────────────────────────────────────────────

/**
 * Lightweight ESP32 client for the Wiener Linien OGD Realtime API.
 *
 * Usage:
 *   OeffiClient client;
 *   client.begin();
 *   auto result = client.getDepartures({5942, 5900}, "WLB");
 *   Serial.println(result.directionH.next().countdown);
 */
class OeffiClient {
public:
    // ── Configuration ────────────────────────

    /** Base URL of the Wiener Linien realtime API. */
    static constexpr const char *API_BASE = "https://www.wienerlinien.at/ogd_realtime/";

    /**
     * Traffic info categories to activate in each request.
     * Comma-separated values that become repeated query parameters.
     */
    static constexpr const char *DEFAULT_TRAFFIC_INFOS =
            "stoerungkurz&activateTrafficInfo=stoerunglang&activateTrafficInfo=aufzugsinfo";

    /** HTTP timeout in milliseconds. */
    static constexpr int HTTP_TIMEOUT_MS = 8000;

    /** Maximum ArduinoJson document size. Increase if parsing fails on large responses. */
    static constexpr size_t JSON_DOC_SIZE = 32768;

    // ── Lifecycle ────────────────────────────

    OeffiClient() = default;

    ~OeffiClient() = default;

    /**
     * Optional: call once to verify WiFi is up before making requests.
     * Returns false if WiFi is not connected.
     */
    bool begin();

    // ── Core API ─────────────────────────────

    /**
     * Fetch and parse departures for given stop IDs, filtered to a specific line name.
     *
     * @param stopIds    List of RBL stop IDs (e.g. {5942, 5900})
     * @param lineName   Line to filter for (e.g. "WLB", "62", "U3", "13A")
     * @return           LineDepartures struct; check .valid before use
     */
    LineDepartures getDepartures(const std::vector<int> &stopIds,
                                 const String &lineName);

    /**
     * Fetch departures for a single stop ID.
     * Convenience overload – delegates to getDepartures(vector, lineName).
     */
    LineDepartures getDepartures(int stopId, const String &lineName);

    /**
     * Fetch multiple lines from the same set of stops in one HTTP call.
     * Useful when several lines share stops to minimise requests.
     *
     * @param stopIds    List of RBL stop IDs
     * @param lineNames  Lines to return (one LineDepartures per line)
     * @return           Vector with one entry per requested line
     */
    std::vector<LineDepartures> getDeparturesMultiLine(
        const std::vector<int> &stopIds,
        const std::vector<String> &lineNames);

    // ── Helpers ──────────────────────────────

    /**
     * Returns the last raw JSON string received (for debugging).
     */
    const String &lastRawJson() const { return _lastRawJson; }

    /**
     * Set a custom HTTP timeout (milliseconds).
     */
    void setHttpTimeout(int ms) { _httpTimeoutMs = ms; }

private:
    String _lastRawJson;
    int _httpTimeoutMs = HTTP_TIMEOUT_MS;

    // ── Internal helpers ─────────────────────
    String _buildUrl(const std::vector<int> &stopIds) const;

    bool _httpGet(const String &url, String &out);

    bool _parseResponse(const String &json,
                        const String &lineName,
                        LineDepartures &result);

    void _parseDisruptions(const JsonObjectConst &data,
                           const String &lineName,
                           String &outMsg);

    void _parseDeparture(const JsonObjectConst &depObj,
                         const JsonObjectConst &lineObj,
                         const String &disruptionMsg,
                         Departure &out);
};
