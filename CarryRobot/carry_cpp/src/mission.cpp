#include "mission.h"
#include "config.h"
#include "globals.h"
#include "hardware.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ctype.h>
#include <algorithm>

WiFiManager wifiManager;

bool mqttOk() { return mqttConnected && mqttClient.connected(); }

void wifiInit(bool forcePortal) {
  WiFi.mode(WIFI_STA);
  wifiManager.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
  wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);

  char portStr[8]; snprintf(portStr, sizeof(portStr), "%d", mqttPort);
  WiFiManagerParameter p_server("mqttServer", "MQTT Server", mqttServer, sizeof(mqttServer) - 1);
  WiFiManagerParameter p_port("mqttPort", "MQTT Port", portStr, 7);
  WiFiManagerParameter p_user("mqttUser", "MQTT User", mqttUser, sizeof(mqttUser) - 1);
  WiFiManagerParameter p_pass("mqttPass", "MQTT Password", mqttPass, sizeof(mqttPass) - 1);

  wifiManager.addParameter(&p_server); wifiManager.addParameter(&p_port);
  wifiManager.addParameter(&p_user); wifiManager.addParameter(&p_pass);

  shouldSaveConfig = false; wifiManager.setSaveConfigCallback([]() { shouldSaveConfig = true; });

  if (forcePortal) { wifiManager.resetSettings(); prefs.begin("carrycfg", false); prefs.clear(); prefs.end(); }
  String apName = String("CarryRobot-") + ROBOT_ID;
  bool res = forcePortal ? wifiManager.startConfigPortal(apName.c_str()) : wifiManager.autoConnect(apName.c_str());

  if (res) {
    const char* newServer = p_server.getValue(); if (newServer && strlen(newServer) > 0) strlcpy(mqttServer, newServer, sizeof(mqttServer));
    const char* newPort = p_port.getValue(); if (newPort && strlen(newPort) > 0) mqttPort = atoi(newPort);
    const char* newUser = p_user.getValue(); if (newUser && strlen(newUser) > 0) strlcpy(mqttUser, newUser, sizeof(mqttUser));
    const char* newPass = p_pass.getValue(); if (newPass && strlen(newPass) > 0) strlcpy(mqttPass, newPass, sizeof(mqttPass));
    if (shouldSaveConfig) { prefs.begin("carrycfg", false); prefs.putString("mqtt_server", mqttServer); prefs.end(); }
  }
}

bool isWiFiConnected() { return WiFi.status() == WL_CONNECTED; }

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char* msg = (char*)malloc(length + 1); if (!msg) return; memcpy(msg, payload, length); msg[length] = '\0';
  if (strcmp(topic, topicMissionAssign) == 0)        parseMissionPayload(msg);
  else if (strcmp(topic, topicMissionCancel) == 0)    parseCancelPayload(msg);
  else if (strcmp(topic, topicMissionReturnRoute) == 0) parseReturnRoutePayload(msg);
  else if (strcmp(topic, topicCommand) == 0)          parseCommandPayload(msg);
  free(msg);
}

void buildTopics() {
  snprintf(topicTelemetry, sizeof(topicTelemetry), TOPIC_TELEMETRY, ROBOT_ID);
  snprintf(topicMissionAssign, sizeof(topicMissionAssign), TOPIC_MISSION_ASSIGN, ROBOT_ID);
  snprintf(topicMissionProgress, sizeof(topicMissionProgress), TOPIC_MISSION_PROGRESS, ROBOT_ID);
  snprintf(topicMissionComplete, sizeof(topicMissionComplete), TOPIC_MISSION_COMPLETE, ROBOT_ID);
  snprintf(topicMissionReturned, sizeof(topicMissionReturned), TOPIC_MISSION_RETURNED, ROBOT_ID);
  snprintf(topicMissionCancel, sizeof(topicMissionCancel), TOPIC_MISSION_CANCEL, ROBOT_ID);
  snprintf(topicMissionReturnRoute, sizeof(topicMissionReturnRoute), TOPIC_MISSION_RETURN_ROUTE, ROBOT_ID);
  snprintf(topicPositionWaitingReturn, sizeof(topicPositionWaitingReturn), TOPIC_POSITION_WAITING_RETURN, ROBOT_ID);
  snprintf(topicCommand, sizeof(topicCommand), TOPIC_COMMAND, ROBOT_ID);
}

void mqttInit() { buildTopics(); mqttClient.setServer(mqttServer, mqttPort); mqttClient.setCallback(mqttCallback); mqttClient.setBufferSize(8192); }

void mqttReconnect() {
  if (mqttClient.connected()) { mqttConnected = true; return; }
  if (!isWiFiConnected()) { mqttConnected = false; return; }
  if (millis() - lastMqttReconnect < MQTT_RECONNECT_MS) return;
  lastMqttReconnect = millis();

  String clientId = String("CarryRobot-") + ROBOT_ID + "-" + String(random(0xffff), HEX);
  if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) {
    mqttConnected = true;
    mqttClient.subscribe(topicMissionAssign); mqttClient.subscribe(topicMissionCancel);
    mqttClient.subscribe(topicMissionReturnRoute); mqttClient.subscribe(topicCommand);
    beepOnce(60, 2400);
  } else { mqttConnected = false; }
}

void mqttLoop() { if (!mqttClient.connected()) mqttReconnect(); mqttClient.loop(); }
bool isMqttConnected() { return mqttClient.connected(); }
void mqttPublish(const char* topic, const String& payload, bool retained) { if (!mqttClient.connected()) return; mqttClient.publish(topic, payload.c_str(), retained); }

void sendTelemetry() {
  if (!mqttClient.connected()) return;
  StaticJsonDocument<384> doc;
  doc["robotId"] = ROBOT_ID; doc["name"] = DEVICE_NAME; doc["type"] = "carry";
  doc["batteryLevel"] = 100; doc["firmwareVersion"] = "carry-mqtt-v3";
  doc["status"] = ((state != ST_IDLE) || (activeMissionId.length() > 0)) ? "busy" : "idle";
  doc["mqttConnected"] = mqttConnected; doc["currentNodeId"] = currentCheckpoint;
  if (bedId.length() > 0) doc["destBed"] = bedId;
  String payload; serializeJson(doc, payload); mqttPublish(topicTelemetry, payload);
}

void sendProgress(const char* statusText, const String& nodeId, const char* note) {
  if (!mqttClient.connected() || activeMissionId.length() == 0) return;
  StaticJsonDocument<256> doc; doc["missionId"] = activeMissionId;
  if (statusText && statusText[0]) doc["status"] = statusText;
  doc["currentNodeId"] = nodeId; doc["batteryLevel"] = 100;
  if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload); mqttPublish(topicMissionProgress, payload);
}

void sendComplete(const char* result) {
  if (!mqttClient.connected() || activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc; doc["missionId"] = activeMissionId; doc["result"] = result;
  doc["note"] = "delivered; switch released; start return";
  String payload; serializeJson(doc, payload); mqttPublish(topicMissionComplete, payload);
}

void sendReturned(const char* note) {
  if (!mqttClient.connected() || activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc; doc["missionId"] = activeMissionId; if (note) doc["note"] = note;
  String payload; serializeJson(doc, payload); mqttPublish(topicMissionReturned, payload);
}

void sendPositionWaitingReturn(const String& currentNodeId) {
  if (!mqttClient.connected() || activeMissionId.length() == 0) return;
  StaticJsonDocument<192> doc; doc["missionId"] = activeMissionId; doc["currentNodeId"] = currentNodeId;
  String payload; serializeJson(doc, payload); mqttPublish(topicPositionWaitingReturn, payload);
}

void parseMissionPayload(const char* payload) {
  StaticJsonDocument<8192> doc; if (deserializeJson(doc, payload)) return;
  JsonVariant m = doc["mission"]; if (m.isNull()) m = doc.as<JsonVariant>();
  String mid = m["missionId"] | ""; if (mid.length() == 0) return;
  if (activeMissionId.length() > 0 && activeMissionId != mid) return;
  if (mid != activeMissionId) { routeIndex = 0; cancelPending = false; destUturnedBeforeWait = false; }
  activeMissionId = mid; activeMissionStatus = String((const char*)(m["status"] | "pending"));
  patientName = String((const char*)(m["patientName"] | "")); bedId = String((const char*)(m["bedId"] | ""));
  if (bedId.length() == 0) bedId = String((const char*)(m["destBed"] | ""));

  auto parseRoute = [](JsonArray arr, std::vector<RoutePoint>& outVec) {
    for (JsonObject p : arr) {
      RoutePoint rp; rp.nodeId = String((const char*)(p["nodeId"] | ""));
      rp.x = p["x"] | 0.0f; rp.y = p["y"] | 0.0f;
      const char* act = p["action"] | ""; rp.action = 'F';
      if (act && act[0]) { char c = (char)toupper(act[0]); if (c=='L'||c=='R'||c=='B'||c=='F') rp.action = c; }
      rp.rfidUid = getUidForNode(rp.nodeId); rp.rfidUid.toUpperCase();
      if (rp.nodeId.length() > 0) outVec.push_back(rp);
    }
  };
  outbound.clear(); retRoute.clear();
  parseRoute(m["outboundRoute"].as<JsonArray>(), outbound); parseRoute(m["returnRoute"].as<JsonArray>(), retRoute);
  if (retRoute.size() < 2 && outbound.size() >= 2) { retRoute = outbound; std::reverse(retRoute.begin(), retRoute.end()); }
  if (state == ST_IDLE) state = ST_GET_MISSION;
  beepOnce(100, 2000); oledDraw();
}

void parseCancelPayload(const char* payload) {
  StaticJsonDocument<256> doc; if (deserializeJson(doc, payload)) return;
  String mid = doc["missionId"] | ""; if (mid.length() == 0 || mid != activeMissionId) return;
  activeMissionStatus = "cancelled";
  if (state == ST_OUTBOUND) { state = ST_CANCEL; cancelPending = true; beepOnce(80, 1500); }
  else if (state == ST_GET_MISSION) { beepOnce(80, 1500); }
  oledDraw();
}

void parseReturnRoutePayload(const char* payload) {
  StaticJsonDocument<4096> doc; if (deserializeJson(doc, payload)) return;
  String mid = doc["missionId"] | ""; if (mid.length() == 0 || mid != activeMissionId) return;
  
  String status = doc["status"] | "";
  if (status != "ok") {
    buildReturnFromVisited(); outbound.clear();
    waitingForReturnRoute = false; state = ST_BACK; routeIndex = 0; obstacleHold = false;
    beepOnce(120, 1800); oledDraw(); return;
  }
  
  retRoute.clear(); JsonArray arr = doc["returnRoute"].as<JsonArray>();
  if (arr.isNull() || arr.size() < 2) {
    buildReturnFromVisited(); outbound.clear();
    waitingForReturnRoute = false; state = ST_BACK; routeIndex = 0; obstacleHold = false;
    beepOnce(120, 1800); oledDraw(); return;
  }
  
  outbound.clear();
  
  for (JsonObject p : arr) {
    RoutePoint rp; rp.nodeId = String((const char*)(p["nodeId"] | ""));
    rp.x = p["x"] | 0.0f; rp.y = p["y"] | 0.0f;
    const char* act = p["action"] | ""; rp.action = 'F';
    if (act && act[0]) { char c = (char)toupper(act[0]); if (c=='L'||c=='R'||c=='B'||c=='F') rp.action = c; }
    rp.rfidUid = getUidForNode(rp.nodeId); rp.rfidUid.toUpperCase();
    if (rp.nodeId.length() > 0) retRoute.push_back(rp);
  }
  waitingForReturnRoute = false; state = ST_BACK; routeIndex = 0; obstacleHold = false;
  beepOnce(120, 2400); oledDraw();
}

void parseCommandPayload(const char* payload) {
  StaticJsonDocument<256> doc; if (deserializeJson(doc, payload)) return;
  String cmd = doc["command"] | "";
  if (cmd == "stop") { motorsStop(); obstacleHold = true; beepOnce(200, 1200); } 
  else if (cmd == "resume") { obstacleHold = false; beepOnce(60, 2400); }
}

const std::vector<RoutePoint>& currentRoute() { return (state == ST_BACK) ? retRoute : outbound; }
String expectedNextUid() { const auto& r = currentRoute(); if (routeIndex + 1 >= (int)r.size()) return ""; return r[routeIndex + 1].rfidUid; }
String currentNodeIdSafe() { const auto& r = currentRoute(); if (routeIndex >= 0 && routeIndex < (int)r.size()) return r[routeIndex].nodeId; return ""; }
char upcomingTurnAtNextNode() { const auto& r = currentRoute(); int idx = routeIndex + 1; if (idx >= 0 && idx < (int)r.size()) { char a = r[idx].action; a = (char)toupper((int)a); if (a == 'L' || a == 'R') return a; } return 'F'; }
const char* turnCharLabel(char a) { if (a == 'L') return "L"; if (a == 'R') return "R"; if (a == 'B') return "B"; return "-"; }
char invertTurn(char a) { a = (char)toupper((int)a); if (a == 'L') return 'R'; if (a == 'R') return 'L'; return 'F'; }

void buildReturnFromVisited() {
  if (outbound.size() < 2) return; if (routeIndex < 0) return;
  if (routeIndex >= (int)outbound.size()) routeIndex = (int)outbound.size() - 1;
  std::vector<RoutePoint> visited(outbound.begin(), outbound.begin() + (routeIndex + 1));
  std::reverse(visited.begin(), visited.end());
  auto findOutAction = [&](const String& nodeId) -> char { for (const auto& p : outbound) { if (p.nodeId == nodeId) return p.action; } return 'F'; };
  for (auto& p : visited) { char oa = findOutAction(p.nodeId); p.action = invertTurn(oa); }
  if (!visited.empty()) visited[0].action = 'F'; retRoute.swap(visited);
}

void startOutbound() { state = ST_OUTBOUND; routeIndex = 0; obstacleHold = false; ignoreNfcFor(600); cancelPending = false; destUturnedBeforeWait = false; driveForward(PWM_FWD); }
void enterWaitAtDest() { state = ST_WAIT_AT_DEST; motorsStop(); beepArrivedPattern(); }
void startReturn(const char* note, bool doUturn) {
  if (retRoute.size() < 2 && outbound.size() >= 2) buildReturnFromVisited();
  state = ST_BACK; routeIndex = 0; obstacleHold = false;
  if (doUturn && !destUturnedBeforeWait) { turnByAction('B'); ignoreNfcFor(900); }
  driveForward(PWM_FWD);
}
void goIdleReset() { state = ST_IDLE; motorsStop(); activeMissionId = ""; activeMissionStatus = ""; patientName = ""; bedId = ""; outbound.clear(); retRoute.clear(); routeIndex = 0; cancelPending = false; destUturnedBeforeWait = false; }

void handleCheckpointHit(const String& uid) {
  String nodeName = uidLookupByUid(uid);
  bool isAtMED = (nodeName == "MED"); bool isHome = (nodeName == "MED") || (nodeName == "H_MED");

  if (nodeName.length() > 0) { currentCheckpoint = nodeName; } 
  else { currentCheckpoint = "UID:" + uid.substring(0, 8); }

  if (state == ST_WAIT_CHECKPOINT && nodeName.length() > 0 && !isAtMED) {
    applyForwardBrake(); 
    cancelAtNodeId = nodeName;
    sendPositionWaitingReturn(cancelAtNodeId); 
    waitingForReturnRoute = true; waitingReturnRouteStartTime = millis();
    state = ST_WAIT_RETURN_ROUTE;
    beepOnce(160, 2000); oledDraw(); return;
  }

  if (isHome && state == ST_BACK) {
    applyForwardBrake(); turnByAction('B'); ignoreNfcFor(900);
    sendReturned(activeMissionStatus == "cancelled" ? "returned_after_cancel" : "returned_ok");
    goIdleReset(); beepOnce(200, 2400); return;
  }

if (isAtMED && state == ST_GET_MISSION) {
    if (digitalRead(SW_PIN) == LOW) { beepOnce(100, 2400); startOutbound(); } 
    else { beepOnce(60, 2000); ignoreNfcFor(2000); }
    return;
  }
  if (isHome && state == ST_IDLE) { ignoreNfcFor(2000); return; }
  if (nodeName.length() == 0) return;
  if (state != ST_OUTBOUND && state != ST_CANCEL && state != ST_BACK) return;
  if (currentRoute().size() < 2) return;
  String expectedUid = expectedNextUid(); if (expectedUid.length() == 0) return;

  if (uid != expectedUid) { applyForwardBrake(); delay(100); driveForward(PWM_FWD); return; }

  applyForwardBrake(); routeIndex++;
  const char* phase = (state == ST_CANCEL) ? "phase:cancel" : (state == ST_OUTBOUND) ? "phase:outbound" : "phase:return";
  const char* statusText = (state == ST_BACK && activeMissionStatus == "cancelled") ? "cancelled" : (state == ST_BACK) ? "completed" : "en_route";
  sendProgress(statusText, currentRoute()[routeIndex].nodeId, phase); beepOnce(60, 2200);

  if (state == ST_CANCEL || (state == ST_OUTBOUND && cancelPending)) {
    cancelPending = false; activeMissionStatus = "cancelled"; turnByAction('B'); ignoreNfcFor(900);
    cancelAtNodeId = currentRoute()[routeIndex].nodeId; sendPositionWaitingReturn(cancelAtNodeId);
    waitingForReturnRoute = true; waitingReturnRouteStartTime = millis(); state = ST_WAIT_RETURN_ROUTE;
    beepOnce(160, 1500); return;
  }

  char a = (char)toupper((int)currentRoute()[routeIndex].action);
  if (a == 'L' || a == 'R') { showTurnOverlay(a, 1500); beepOnce(60, 2000); turnByAction(a); ignoreNfcFor(700); }

  if (state == ST_OUTBOUND && routeIndex >= (int)outbound.size() - 1) {
    applyForwardBrake(); toneOff(); turnByAction('B'); destUturnedBeforeWait = true; ignoreNfcFor(900); enterWaitAtDest(); return;
  }
  driveForward(PWM_FWD);
}