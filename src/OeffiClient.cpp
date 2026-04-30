#include "OeffiClient.h"

// ─────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────

bool OeffiClient::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OeffiClient] WARNING: WiFi not connected.");
        return false;
    }
    return true;
}

// ─────────────────────────────────────────────
//  Public API
// ─────────────────────────────────────────────

LineDepartures OeffiClient::getDepartures(int stopId, const String& lineName) {
    return getDepartures(std::vector<int>{stopId}, lineName);
}

LineDepartures OeffiClient::getDepartures(const std::vector<int>& stopIds,
                                          const String& lineName) {
    LineDepartures result;
    result.lineName = lineName;
    result.directionH.direction = "H";
    result.directionR.direction = "R";

    String url = _buildUrl(stopIds);
    String json;

    if (!_httpGet(url, json)) {
        result.valid    = false;
        result.errorMsg = "HTTP request failed";
        return result;
    }

    if (!_parseResponse(json, lineName, result)) {
        result.valid    = false;
        // errorMsg set inside _parseResponse
        return result;
    }

    result.valid = true;
    return result;
}

std::vector<LineDepartures> OeffiClient::getDeparturesMultiLine(
    const std::vector<int>& stopIds,
    const std::vector<String>& lineNames)
{
    std::vector<LineDepartures> results;

    String url = _buildUrl(stopIds);
    String json;

    if (!_httpGet(url, json)) {
        // Fill every requested line with an error entry
        for (const auto& name : lineNames) {
            LineDepartures err;
            err.lineName = name;
            err.valid    = false;
            err.errorMsg = "HTTP request failed";
            results.push_back(err);
        }
        return results;
    }

    for (const auto& name : lineNames) {
        LineDepartures ld;
        ld.lineName         = name;
        ld.directionH.direction = "H";
        ld.directionR.direction = "R";

        if (_parseResponse(json, name, ld)) {
            ld.valid = true;
        } else {
            ld.valid = false;
        }
        results.push_back(ld);
    }

    return results;
}

// ─────────────────────────────────────────────
//  URL builder
// ─────────────────────────────────────────────

String OeffiClient::_buildUrl(const std::vector<int>& stopIds) const {
    String url = String(API_BASE) + "monitor?";

    bool first = true;
    for (int id : stopIds) {
        if (!first) url += "&";
        url += "stopId=" + String(id);
        first = false;
    }

    url += "&activateTrafficInfo=";
    url += String(DEFAULT_TRAFFIC_INFOS);

    return url;
}

// ─────────────────────────────────────────────
//  HTTP GET
// ─────────────────────────────────────────────

bool OeffiClient::_httpGet(const String& url, String& out) {
    HTTPClient http;
    http.setTimeout(_httpTimeoutMs);
    http.begin(url);

    int code = http.GET();

    if (code != HTTP_CODE_OK) {
        Serial.printf("[OeffiClient] HTTP error %d for URL: %s\n", code, url.c_str());
        http.end();
        return false;
    }

    out = http.getString();
    _lastRawJson = out;
    http.end();

    if (out.isEmpty()) {
        Serial.println("[OeffiClient] Empty response body.");
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────
//  JSON Parsing – disruptions
// ─────────────────────────────────────────────

/**
 * Scans the top-level trafficInfos array for any entry whose relatedLines
 * list contains lineName.  Sets outMsg to the first match's title.
 */
void OeffiClient::_parseDisruptions(const JsonObjectConst& data,
                                    const String& lineName,
                                    String& outMsg) {
    outMsg = "";

    JsonArrayConst infos = data["trafficInfos"].as<JsonArrayConst>();
    if (infos.isNull()) return;

    for (JsonObjectConst ti : infos) {
        JsonArrayConst related = ti["relatedLines"].as<JsonArrayConst>();
        if (related.isNull()) continue;

        for (JsonVariantConst line : related) {
            if (line.as<String>() == lineName) {
                outMsg = ti["title"].as<String>();
                return; // first match is enough
            }
        }
    }
}

// ─────────────────────────────────────────────
//  JSON Parsing – single departure
// ─────────────────────────────────────────────

void OeffiClient::_parseDeparture(const JsonObjectConst& depObj,
                                  const JsonObjectConst& lineObj,
                                  const String& disruptionMsg,
                                  Departure& out) {
    // ── Countdown / times ──
    JsonObjectConst dt = depObj["departureTime"].as<JsonObjectConst>();
    if (!dt.isNull()) {
        out.countdown     = dt["countdown"]   | -1;
        out.timePlanned   = dt["timePlanned"] | "";
        out.timeReal      = dt["timeReal"]    | "";
    }

    // ── Destination / flags – prefer vehicle block, fall back to line ──
    JsonObjectConst vehicle = depObj["vehicle"].as<JsonObjectConst>();

    if (!vehicle.isNull()) {
        out.towards           = vehicle["towards"]          | lineObj["towards"].as<const char*>();
        out.barrierFree       = vehicle["barrierFree"]      | false;
        out.realtimeSupported = vehicle["realtimeSupported"] | false;
        out.trafficJam        = vehicle["trafficjam"]       | false;
    } else {
        out.towards           = lineObj["towards"]          | "";
        out.barrierFree       = lineObj["barrierFree"]      | false;
        out.realtimeSupported = lineObj["realtimeSupported"] | false;
        out.trafficJam        = lineObj["trafficjam"]       | false;
    }

    out.disruptionMessage = disruptionMsg;
}

// ─────────────────────────────────────────────
//  JSON Parsing – full response
// ─────────────────────────────────────────────

bool OeffiClient::_parseResponse(const String& json,
                                  const String& lineName,
                                  LineDepartures& result) {
    // Allocate JSON document on heap – ESP32 heap is generous enough
    DynamicJsonDocument doc(JSON_DOC_SIZE);

    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        result.errorMsg = String("JSON parse error: ") + err.c_str();
        Serial.printf("[OeffiClient] %s\n", result.errorMsg.c_str());
        return false;
    }

    JsonObjectConst data = doc["data"].as<JsonObjectConst>();
    if (data.isNull()) {
        result.errorMsg = "No 'data' field in response";
        return false;
    }

    // ── Global disruption message for this line ──
    String globalDisruption;
    _parseDisruptions(data, lineName, globalDisruption);

    // ── Iterate monitors ──
    JsonArrayConst monitors = data["monitors"].as<JsonArrayConst>();
    if (monitors.isNull()) {
        result.errorMsg = "No monitors in response";
        return false;
    }

    bool foundAny = false;

    for (JsonObjectConst monitor : monitors) {
        // Extract stop title from the first matching monitor
        if (result.stopTitle.isEmpty()) {
            const char* title = monitor["locationStop"]["properties"]["title"];
            if (title) result.stopTitle = title;

            const int rbl = monitor["locationStop"]["properties"]["attributes"]["rbl"] | 0;
            if (rbl > 0 && result.stopId == 0) result.stopId = rbl;
        }

        JsonArrayConst lines = monitor["lines"].as<JsonArrayConst>();
        if (lines.isNull()) continue;

        for (JsonObjectConst lineObj : lines) {
            // Filter by requested line name
            const char* name = lineObj["name"];
            if (!name || lineName != name) continue;

            const char* dir = lineObj["direction"] | "";

            // Decide which direction bucket to fill
            DirectionDepartures* bucket = nullptr;
            if (strcmp(dir, "H") == 0) {
                bucket = &result.directionH;
                if (result.directionH.towards.isEmpty())
                    result.directionH.towards = lineObj["towards"] | "";
            } else if (strcmp(dir, "R") == 0) {
                bucket = &result.directionR;
                if (result.directionR.towards.isEmpty())
                    result.directionR.towards = lineObj["towards"] | "";
            } else {
                continue; // unexpected direction value – skip
            }

            JsonArrayConst departures = lineObj["departures"]["departure"]
                                            .as<JsonArrayConst>();
            if (departures.isNull()) continue;

            for (JsonObjectConst depObj : departures) {
                Departure dep;
                dep.direction = String(dir);
                _parseDeparture(depObj, lineObj, globalDisruption, dep);
                bucket->departures.push_back(dep);
                foundAny = true;
            }
        }
    }

    if (!foundAny) {
        // Not necessarily an error – line may just have no departures right now
        Serial.printf("[OeffiClient] No departures found for line '%s'\n",
                      lineName.c_str());
    }

    return true; // parsed successfully even if zero departures
}
