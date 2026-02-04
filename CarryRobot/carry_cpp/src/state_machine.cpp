#include "state_machine.h"
#include "config.h"
#include "globals.h"
#include "motor_control.h"
#include "display.h"
#include "communication.h"
#include "route_logic.h"
#include "helpers.h"

// =========================================
// STATE TRANSITIONS
// =========================================
void startOutbound() {
  robotState = STATE_RUN_OUTBOUND;
  outboundIdx = 0;
  visitedLen = 0;
  
#if SERIAL_DEBUG
  Serial.println("STATE -> RUN_OUTBOUND");
  Serial.print("Route length: "); Serial.println(outboundLen);
#endif
  
  if (outboundLen > 0) {
    drawRouteProgress("OUT", 0, outboundLen, outboundRoute[0].node);
  }
  
  driveForward(PWM_FWD);
}

void enterWaitCargo() {
  robotState = STATE_WAIT_CARGO;
  motorsStop();
  
#if SERIAL_DEBUG
  Serial.println("STATE -> WAIT_CARGO");
#endif
  
  drawWaitingCargo(currentMissionId, missionDestBed);
  updateMissionProgress(currentMissionId, "MED", "waiting_cargo", 10);
}

void enterWaitAtDest() {
  robotState = STATE_WAIT_AT_DEST;
  motorsStop();
  
#if SERIAL_DEBUG
  Serial.println("STATE -> WAIT_AT_DEST");
#endif
  
  drawCentered("ARRIVED", missionDestBed, "Waiting for", "cargo unload");
  updateMissionProgress(currentMissionId, missionDestBed, "at_destination", 70);
}

void startReturn() {
  robotState = STATE_RUN_RETURN;
  
  buildReturnFromVisited(visitedNodes, visitedLen, returnRoute, returnLen);
  returnIdx = 0;
  
#if SERIAL_DEBUG
  Serial.println("STATE -> RUN_RETURN");
  Serial.print("Return route length: "); Serial.println(returnLen);
#endif
  
  if (returnLen > 0) {
    turnByAction(returnRoute[0].action);
    drawRouteProgress("RET", 0, returnLen, returnRoute[0].node);
  }
  
  driveForward(PWM_FWD);
}

void enterIdle() {
  robotState = STATE_IDLE_AT_MED;
  motorsStop();
  
  currentMissionId[0] = '\0';
  missionDestBed[0] = '\0';
  clearRoute(outboundRoute, outboundLen);
  clearRoute(returnRoute, returnLen);
  visitedLen = 0;
  
#if SERIAL_DEBUG
  Serial.println("STATE -> IDLE_AT_MED");
#endif
  
  drawCentered("IDLE", "At MED station", "Waiting for", "mission...");
  
  completeMission(currentMissionId);
  buzzOK();
}

// =========================================
// CHECKPOINT HANDLER
// =========================================
void handleCheckpointHit(const char* nodeName) {
  strncpy(lastVisitedNode, nodeName, MAX_NODE_LEN - 1);
  lastVisitedNode[MAX_NODE_LEN - 1] = '\0';
  
#if SERIAL_DEBUG
  Serial.print("Checkpoint: "); Serial.println(nodeName);
#endif
  
  switch (robotState) {
    case STATE_RUN_OUTBOUND: {
      if (outboundIdx < outboundLen) {
        const char* expected = expectedNextUid(outboundRoute, outboundLen, outboundIdx);
        
        if (expected && strcmp(nodeName, expected) == 0) {
          // Store visited node
          if (visitedLen < MAX_ROUTE_LEN) {
            strncpy(visitedNodes[visitedLen], nodeName, MAX_NODE_LEN - 1);
            visitedNodes[visitedLen][MAX_NODE_LEN - 1] = '\0';
            visitedLen++;
          }
          
          char action = outboundRoute[outboundIdx].action;
          outboundIdx++;
          
          // Check if reached destination
          if (outboundIdx >= outboundLen) {
            applyForwardBrake(PWM_BRAKE, BRAKE_FORWARD_MS);
            enterWaitAtDest();
            return;
          }
          
          // Apply forward brake before turn
          if (action == 'L' || action == 'R' || action == 'B') {
            applyForwardBrake(PWM_BRAKE, BRAKE_FORWARD_MS);
            turnByAction(action);
          }
          
          int progress = (outboundIdx * 50) / outboundLen;
          updateMissionProgress(currentMissionId, nodeName, "in_transit", progress);
          drawRouteProgress("OUT", outboundIdx, outboundLen, outboundRoute[outboundIdx].node);
          
          driveForward(PWM_FWD);
        }
      }
      break;
    }
    
    case STATE_RUN_RETURN: {
      if (returnIdx < returnLen) {
        const char* expected = expectedNextUid(returnRoute, returnLen, returnIdx);
        
        if (expected && strcmp(nodeName, expected) == 0) {
          char action = returnRoute[returnIdx].action;
          returnIdx++;
          
          // Check if returned to MED
          if (returnIdx >= returnLen || strcmp(nodeName, "MED") == 0 || strcmp(nodeName, "H_MED") == 0) {
            applyForwardBrake(PWM_BRAKE, BRAKE_FORWARD_MS);
            enterIdle();
            return;
          }
          
          // Apply forward brake before turn
          if (action == 'L' || action == 'R' || action == 'B') {
            applyForwardBrake(PWM_BRAKE, BRAKE_FORWARD_MS);
            turnByAction(action);
          }
          
          int progress = 70 + (returnIdx * 30) / returnLen;
          updateMissionProgress(currentMissionId, nodeName, "returning", progress);
          drawRouteProgress("RET", returnIdx, returnLen, returnRoute[returnIdx].node);
          
          driveForward(PWM_FWD);
        }
      }
      break;
    }
    
    default:
      break;
  }
}
