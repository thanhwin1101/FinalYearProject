/*  mission_delegate.h  –  Send routes to Slave and start autonomous mission
 */
#pragma once

// Call after routeParseAssign(): send route chunks to Slave only (no start). Used on mission/assign.
void missionDelegateSendRoutesOnly();

// Call when user has scanned MED + pressed SW: send missionStart and enter ST_MISSION_DELEGATED.
void missionDelegateStartMission();
