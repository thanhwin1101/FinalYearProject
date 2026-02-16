#ifndef ROUTE_LOGIC_H
#define ROUTE_LOGIC_H

#include <Arduino.h>
#include <vector>
#include "globals.h"

// =========================================
// ROUTE FUNCTIONS
// =========================================

// Get current active route based on state
const std::vector<RoutePoint>& currentRoute();

// Get expected next UID
String expectedNextUid();

// Get current node ID safely
String currentNodeIdSafe();

// Get upcoming turn at next node
char upcomingTurnAtNextNode();

// Turn character label
const char* turnCharLabel(char a);

// Invert a turn action
char invertTurn(char a);

// Build return route from visited nodes
void buildReturnFromVisited();

#endif
