#include "globals.h"

volatile RobotMode g_mode = MODE_AUTO;

RoutePoint g_route[MAX_ROUTE_LEN];
uint8_t    g_routeLen  = 0;
uint8_t    g_routeIdx  = 0;

volatile int16_t g_cmdVx  = 0;
volatile int16_t g_cmdVy  = 0;
volatile int16_t g_cmdVr  = 0;
volatile bool    g_newVelCmd = false;

volatile int16_t g_wheelFL = 0;
volatile int16_t g_wheelFR = 0;
volatile int16_t g_wheelBL = 0;
volatile int16_t g_wheelBR = 0;
volatile bool    g_newWheelCmd = false;

volatile bool    g_missionStart   = false;
volatile bool    g_missionCancel  = false;
volatile bool    g_missionRunning = false;

volatile bool    g_obstacleDetected = false;

volatile uint16_t g_lastNfcId = 0;
volatile bool     g_newNfc    = false;

volatile uint8_t  g_runSpeed  = MOTOR_RUN_SPEED;
volatile uint8_t  g_turnSpeed = MOTOR_TURN_SPEED;
