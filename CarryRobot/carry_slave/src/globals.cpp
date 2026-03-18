#include "globals.h"

volatile MasterToSlaveMsg masterCmd    = {};
volatile bool             masterCmdNew = false;

SlaveToMasterMsg slaveReport = {};

volatile bool lineFollowActive = false;
volatile bool rfidActive       = false;

unsigned long lastEspnowTxMs = 0;
unsigned long lastNfcReadMs  = 0;
unsigned long lastLineMs     = 0;
