#include "route_logic.h"

// =========================================
// ROUTE FUNCTIONS
// =========================================
void clearRoute(RouteStep* route, int& len) {
  for (int i = 0; i < MAX_ROUTE_LEN; i++) {
    route[i].node[0] = '\0';
    route[i].action = '\0';
  }
  len = 0;
}

const char* expectedNextUid(RouteStep* route, int routeLen, int idx) {
  if (idx < 0 || idx >= routeLen) return nullptr;
  return route[idx].node;
}

static char invertAction(char a) {
  if (a == 'L') return 'R';
  if (a == 'R') return 'L';
  return a;
}

void buildReturnFromVisited(const char visited[][MAX_NODE_LEN], int visitedLen, RouteStep* ret, int& retLen) {
  retLen = 0;
  if (visitedLen < 2) return;
  
  for (int i = visitedLen - 1; i >= 0 && retLen < MAX_ROUTE_LEN; i--) {
    strncpy(ret[retLen].node, visited[i], MAX_NODE_LEN - 1);
    ret[retLen].node[MAX_NODE_LEN - 1] = '\0';
    
    if (retLen == 0) {
      ret[retLen].action = 'B';
    } else {
      ret[retLen].action = 'F';
    }
    retLen++;
  }
}
