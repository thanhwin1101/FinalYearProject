#ifndef ROUTE_LOGIC_H
#define ROUTE_LOGIC_H

#include <Arduino.h>
#include "config.h"

// =========================================
// ROUTE STRUCTURES
// =========================================
struct RouteStep {
  char node[MAX_NODE_LEN];
  char action;
};

// =========================================
// ROUTE FUNCTIONS
// =========================================
void clearRoute(RouteStep* route, int& len);
const char* expectedNextUid(RouteStep* route, int routeLen, int idx);
void buildReturnFromVisited(const char visited[][MAX_NODE_LEN], int visitedLen, RouteStep* ret, int& retLen);

#endif
