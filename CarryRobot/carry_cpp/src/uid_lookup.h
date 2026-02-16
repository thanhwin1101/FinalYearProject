#ifndef UID_LOOKUP_H
#define UID_LOOKUP_H

#include <Arduino.h>

// Node name lookup by raw UID bytes
const char* uidLookupByNodeId(const uint8_t* uid, uint8_t len);

// Node name lookup by hex String UID (e.g., "04381AD2060000")
String uidLookupByUid(const String& uidHex);

// Get UID hex string for a node name
String getUidForNode(const String& nodeName);

// HOME_MED_UID constant
extern const String HOME_MED_UID;

#endif
