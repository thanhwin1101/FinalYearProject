#include "globals.h"

volatile RobotState  robotState          = ST_IDLE;
volatile RobotState  stateBeforeObstacle = ST_IDLE;

volatile SlaveToMasterMsg slaveMsg  = {};
volatile bool             slaveMsgNew = false;

MasterToSlaveMsg masterMsg = {};

String missionId;
String patientName;
String destBed;
String currentNodeIdLive = "---";

volatile bool  obstacleHold      = false;
volatile bool  obstacleBeepFlag  = false;

bool   targetLocked     = false;
bool   servoXLocked     = true;
float  lastServoXAngle  = 90.0f;

uint8_t  recoveryCheckpointsHit = 0;
String   recoveryCpUids[2];

unsigned long lastTelemetryMs    = 0;
unsigned long lastOledMs         = 0;
unsigned long lastTofMs          = 0;
unsigned long lastUsMs           = 0;
unsigned long lastHuskyMs        = 0;
unsigned long lastEspnowTxMs    = 0;
unsigned long lastObstacleBeepMs = 0;

SemaphoreHandle_t g_i2cMutex = nullptr;

volatile bool     g_tofValid  = false;
volatile uint16_t g_tofMm     = 0;
volatile long     g_usLeftMm  = -1;
volatile long     g_usRightMm = -1;
