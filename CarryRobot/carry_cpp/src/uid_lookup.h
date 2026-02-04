#ifndef UID_LOOKUP_H
#define UID_LOOKUP_H

#include <Arduino.h>

// Node name lookup by UID
const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len);

#endif
