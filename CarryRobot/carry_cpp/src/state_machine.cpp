#include "state_machine.h"
#include "config.h"
#include "globals.h"
#include "motor_control.h"
#include "display.h"
#include "communication.h"
#include "route_logic.h"
#include "helpers.h"
#include "uid_lookup.h"
#include <ctype.h>
#include <algorithm>

// =========================================
// STATE TRANSITIONS
// =========================================
void startOutbound() {
  state = RUN_OUTBOUND;
  routeIndex = 0;
  obstacleHold = false;
  ignoreNfcFor(600);
  cancelPending = false;
  destUturnedBeforeWait = false;

#if SERIAL_DEBUG
  Serial.println("STATE -> RUN_OUTBOUND");
  Serial.print("Route length: "); Serial.println(outbound.size());
#endif
  
  driveForward(PWM_FWD);
}

void enterWaitAtDest() {
  state = WAIT_AT_DEST;
  motorsStop();
  
#if SERIAL_DEBUG
  Serial.println("STATE -> WAIT_AT_DEST");
#endif
  
  beepArrivedPattern();
}

void startReturn(const char* note, bool doUturn) {
  if (retRoute.size() < 2 && outbound.size() >= 2) {
    retRoute = outbound;
    std::reverse(retRoute.begin(), retRoute.end());
  }
  
  state = RUN_RETURN;
  routeIndex = 0;
  obstacleHold = false;
  
  if (doUturn && !destUturnedBeforeWait) {
    turnByAction('B');
    ignoreNfcFor(900);
  }
  
#if SERIAL_DEBUG
  Serial.println("STATE -> RUN_RETURN");
  Serial.print("Note: "); Serial.println(note ? note : "none");
  Serial.print("Return route length: "); Serial.println(retRoute.size());
#endif
  
  driveForward(PWM_FWD);
}

void goIdleReset() {
  state = IDLE_AT_MED;
  motorsStop();
  
  activeMissionId = "";
  activeMissionStatus = "";
  patientName = "";
  bedId = "";
  outbound.clear();
  retRoute.clear();
  routeIndex = 0;
  cancelPending = false;
  destUturnedBeforeWait = false;
  
#if SERIAL_DEBUG
  Serial.println("STATE -> IDLE_AT_MED");
#endif
}

// =========================================
// CHECKPOINT HANDLER
// =========================================
void handleCheckpointHit(const String& uid) {
  // Home checkpoint (MED)
  if (uid == HOME_MED_UID) {
    haveSeenMED = true;
    if (state == RUN_RETURN) {
      sendReturned(activeMissionStatus == "cancelled" ? "returned_after_cancel" : "returned_ok");
      goIdleReset();
      beepOnce(200, 2400);
      return;
    }
    if (state == IDLE_AT_MED) {
      beepOnce(60, 2000);
      return;
    }
  }

  // Find node by UID
  String nodeName = uidLookupByUid(uid);
  if (nodeName.length() == 0) {
#if SERIAL_DEBUG
    Serial.print("Unknown UID: "); Serial.println(uid);
#endif
    return;
  }

#if SERIAL_DEBUG
  Serial.print("Checkpoint: "); Serial.print(nodeName);
  Serial.print(" (UID: "); Serial.print(uid); Serial.println(")");
#endif

  const auto& route = currentRoute();
  if (route.size() < 2) return;

  String expectedUid = expectedNextUid();
  if (expectedUid.length() == 0 || uid != expectedUid) {
#if SERIAL_DEBUG
    Serial.print("UID mismatch. Expected: "); Serial.println(expectedUid);
#endif
    return;
  }

  // Checkpoint matched - stop and process
  applyForwardBrake();
  routeIndex++;
  
  if (state == RUN_OUTBOUND) {
    sendProgress("en_route", route[routeIndex].nodeId, "phase:outbound");
  } else {
    const char* st = (activeMissionStatus == "cancelled") ? "cancelled" : "completed";
    sendProgress(st, route[routeIndex].nodeId, "phase:return");
  }
  beepOnce(60, 2200);

  // Handle cancel during outbound
  if (state == RUN_OUTBOUND && cancelPending) {
    cancelPending = false;
    activeMissionStatus = "cancelled";
    
    // Do U-turn first
    applyForwardBrake();
    turnByAction('B');
    ignoreNfcFor(900);
    
    // Get current node ID
    String currentNode = route[routeIndex].nodeId;
    cancelAtNodeId = currentNode;
    
    // Send position to Backend and wait for return route
    sendPositionWaitingReturn(currentNode);
    waitingForReturnRoute = true;
    waitingReturnRouteStartTime = millis();
    state = WAIT_FOR_RETURN_ROUTE;
    
#if SERIAL_DEBUG
    Serial.print("Cancel at checkpoint: "); Serial.println(currentNode);
    Serial.println("Waiting for return route from Backend...");
#endif
    beepOnce(160, 1500);
    return;
  }

  // Handle turns
  char a = (char)toupper((int)route[routeIndex].action);
  if (a == 'L' || a == 'R') {
    showTurnOverlay(a, 1500);
    beepOnce(60, 2000);
    turnByAction(a);
    ignoreNfcFor(700);
  }

  // Check if reached destination (outbound)
  if (state == RUN_OUTBOUND && routeIndex >= (int)outbound.size() - 1) {
    applyForwardBrake();
    toneOff();
    turnByAction('B');  // U-Turn
    destUturnedBeforeWait = true;
    ignoreNfcFor(900);
    enterWaitAtDest();
    return;
  }

  // Check if returned to MED
  if (state == RUN_RETURN && routeIndex >= (int)retRoute.size() - 1) {
    sendReturned(activeMissionStatus == "cancelled" ? "returned_after_cancel" : "returned_ok");
    goIdleReset();
    beepOnce(200, 2400);
    return;
  }
}
