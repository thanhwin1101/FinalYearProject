#include "communication.h"
#include "config.h"
#include "globals.h"
#include "helpers.h"
#include "route_logic.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// =========================================
// WIFI FUNCTIONS
// =========================================
void wifiInit() {
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.setConnectTimeout(30);
  
  WiFiManagerParameter customApiBase("apibase", "API Base URL", apiBaseUrl, sizeof(apiBaseUrl));
  wifiManager.addParameter(&customApiBase);
  
  bool res = wifiManager.autoConnect("CarryRobot-Setup", "carry123");
  
  if (res) {
    Serial.println("WiFi connected");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
    
    strncpy(apiBaseUrl, customApiBase.getValue(), sizeof(apiBaseUrl) - 1);
    apiBaseUrl[sizeof(apiBaseUrl) - 1] = '\0';
    
    prefs.begin("carry", false);
    prefs.putString("apibase", apiBaseUrl);
    prefs.end();
    
    Serial.print("API Base: "); Serial.println(apiBaseUrl);
  } else {
    Serial.println("WiFi failed to connect");
  }
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// =========================================
// API FUNCTIONS
// =========================================
bool fetchNextMission() {
  if (!isWiFiConnected()) return false;
  
  HTTPClient http;
  String url = buildUrl("/api/missions/next/" ROBOT_ID);
  
  http.begin(url);
  http.setTimeout(HTTP_TIMEOUT);
  int code = http.GET();
  
  if (code == 200) {
    String payload = http.getString();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    
    if (!err && doc.containsKey("_id")) {
      strncpy(currentMissionId, doc["_id"] | "", sizeof(currentMissionId) - 1);
      strncpy(missionDestBed, doc["destBed"] | "", sizeof(missionDestBed) - 1);
      
      JsonArray outArr = doc["outboundRoute"].as<JsonArray>();
      outboundLen = 0;
      for (JsonObject step : outArr) {
        if (outboundLen >= MAX_ROUTE_LEN) break;
        strncpy(outboundRoute[outboundLen].node, step["node"] | "", MAX_NODE_LEN - 1);
        const char* act = step["action"] | "F";
        outboundRoute[outboundLen].action = act[0];
        outboundLen++;
      }
      
      http.end();
      return true;
    }
  }
  
  http.end();
  return false;
}

bool updateMissionProgress(const char* missionId, const char* currentNode, const char* status, int progress) {
  if (!isWiFiConnected()) return false;
  
  HTTPClient http;
  String url = buildUrl("/api/missions/");
  url += missionId;
  url += "/progress";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  JsonDocument doc;
  doc["currentNode"] = currentNode;
  doc["status"] = status;
  doc["progress"] = progress;
  
  String body;
  serializeJson(doc, body);
  
  int code = http.PUT(body);
  http.end();
  
  return code == 200;
}

bool completeMission(const char* missionId) {
  if (!isWiFiConnected()) return false;
  
  HTTPClient http;
  String url = buildUrl("/api/missions/");
  url += missionId;
  url += "/progress";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  JsonDocument doc;
  doc["status"] = "completed";
  doc["progress"] = 100;
  
  String body;
  serializeJson(doc, body);
  
  int code = http.PUT(body);
  http.end();
  
  return code == 200;
}

bool sendTelemetry() {
  if (!isWiFiConnected()) return false;
  
  HTTPClient http;
  String url = buildUrl("/api/robots/" ROBOT_ID "/telemetry");
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(HTTP_TIMEOUT);
  
  JsonDocument doc;
  doc["status"] = buildStatusStr();
  doc["battery"] = 100;
  doc["currentNode"] = strlen(lastVisitedNode) > 0 ? lastVisitedNode : "MED";
  
  String body;
  serializeJson(doc, body);
  
  int code = http.PUT(body);
  http.end();
  
  return code == 200;
}
