#include "globals.h"

volatile RobotMode g_mode = MODE_AUTO;

RoutePoint g_route[MAX_ROUTE_LEN];
uint8_t    g_routeLen  = 0;
uint8_t    g_routeIdx  = 0;

volatile int16_t g_cmdVx  = 0;
volatile int16_t g_cmdVy  = 0;
volatile int16_t g_cmdVr  = 0;
volatile bool    g_newVelCmd = false;

volatile bool    g_missionStart   = false;
volatile bool    g_missionCancel  = false;
volatile bool    g_missionRunning = false;

volatile bool    g_obstacleDetected = false;

volatile uint16_t g_lastNfcId = 0;
volatile bool     g_newNfc    = false;
