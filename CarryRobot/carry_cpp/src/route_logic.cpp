#include "route_logic.h"
#include "config.h"
#include <algorithm>
#include <ctype.h>

// =========================================
// ROUTE FUNCTIONS
// =========================================

const std::vector<RoutePoint>& currentRoute() {
  return (state == RUN_RETURN) ? retRoute : outbound;
}

String expectedNextUid() {
  const auto& r = currentRoute();
  if (routeIndex + 1 >= (int)r.size()) return "";
  return r[routeIndex + 1].rfidUid;
}

String currentNodeIdSafe() {
  const auto& r = currentRoute();
  if (routeIndex >= 0 && routeIndex < (int)r.size()) return r[routeIndex].nodeId;
  return "";
}

char upcomingTurnAtNextNode() {
  const auto& r = currentRoute();
  int idx = routeIndex + 1;
  if (idx >= 0 && idx < (int)r.size()) {
    char a = r[idx].action;
    a = (char)toupper((int)a);
    if (a == 'L' || a == 'R') return a;
  }
  return 'F';
}

const char* turnCharLabel(char a) {
  if (a == 'L') return "L";
  if (a == 'R') return "R";
  if (a == 'B') return "B";
  return "-";
}

char invertTurn(char a) {
  a = (char)toupper((int)a);
  if (a == 'L') return 'R';
  if (a == 'R') return 'L';
  return 'F';
}

void buildReturnFromVisited() {
  if (outbound.size() < 2) return;
  if (routeIndex < 0) return;
  if (routeIndex >= (int)outbound.size()) routeIndex = (int)outbound.size() - 1;

  std::vector<RoutePoint> visited(outbound.begin(), outbound.begin() + (routeIndex + 1));
  std::reverse(visited.begin(), visited.end());
  
  auto findOutAction = [&](const String& nodeId) -> char {
    for (const auto& p : outbound) {
      if (p.nodeId == nodeId) return p.action;
    }
    return 'F';
  };

  for (auto& p : visited) {
    char oa = findOutAction(p.nodeId);
    p.action = invertTurn(oa);
  }

  if (!visited.empty()) visited[0].action = 'F';
  retRoute.swap(visited);
}
