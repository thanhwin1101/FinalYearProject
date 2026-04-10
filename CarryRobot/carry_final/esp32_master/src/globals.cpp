#include "globals.h"

volatile RobotMode  g_mode            = MODE_AUTO;
volatile AutoState  g_autoState       = AUTO_IDLE;
volatile uint8_t    g_batteryPercent  = 100;
volatile bool       g_mqttConnected   = false;
volatile bool       g_wifiConnected   = false;

RoutePoint g_route[MAX_ROUTE_LEN];
uint8_t    g_routeLen  = 0;
uint8_t    g_routeIdx  = 0;

char g_patientName[32] = "";
char g_destination[16] = "";
char g_missionId[24]   = "";

volatile uint16_t g_lastCheckpointId   = 0;
volatile bool     g_newCheckpoint      = false;

volatile bool     g_stm32Obstacle      = false;
volatile bool     g_stm32MissionDone   = false;
volatile uint16_t g_stm32MismatchGot   = 0;
volatile uint16_t g_stm32MismatchExp   = 0;
volatile bool     g_stm32MismatchFlag  = false;

volatile bool     g_btnSingleClick = false;
volatile bool     g_btnDoubleClick = false;
volatile bool     g_btnLongPress   = false;

volatile bool     g_mqttCancel     = false;

volatile uint16_t g_tuneSpinMs    = 974;   // default from config
volatile uint16_t g_tuneBrakeMs   = 80;
volatile uint16_t g_tuneWallCm    = 30;
volatile bool     g_testDashboard = false;
volatile bool     g_running       = false;
volatile bool     g_stopped       = false;
volatile bool     g_modeChangeReq = false;
