#include "fsr_handler.h"
#include "config.h"

FootSensor footL, footR;

void fsrInit() {
    pinMode(FSR_L_FRONT, INPUT); pinMode(FSR_L_HEEL, INPUT);
    pinMode(FSR_L_LEFT, INPUT);  pinMode(FSR_L_RIGHT, INPUT);
    pinMode(FSR_R_FRONT, INPUT); pinMode(FSR_R_HEEL, INPUT);
    pinMode(FSR_R_LEFT, INPUT);  pinMode(FSR_R_RIGHT, INPUT);
}

void fsrUpdate() {
    footL.front = analogRead(FSR_L_FRONT); footL.heel = analogRead(FSR_L_HEEL);
    footL.left  = analogRead(FSR_L_LEFT);  footL.right = analogRead(FSR_L_RIGHT);
    footL.total = footL.front + footL.heel + footL.left + footL.right;
    footL.isGrounded = (footL.total > FSR_CONTACT_THRES);

    footR.front = analogRead(FSR_R_FRONT); footR.heel = analogRead(FSR_R_HEEL);
    footR.left  = analogRead(FSR_R_LEFT);  footR.right = analogRead(FSR_R_RIGHT);
    footR.total = footR.front + footR.heel + footR.left + footR.right;
    footR.isGrounded = (footR.total > FSR_CONTACT_THRES);

    if (footL.isGrounded) {
        footL.copX = (float)(footL.front - footL.heel) / footL.total;
        footL.copY = (float)(footL.left - footL.right) / footL.total; 
    } else { footL.copX = footL.copY = 0; }

    if (footR.isGrounded) {
        footR.copX = (float)(footR.front - footR.heel) / footR.total;
        footR.copY = (float)(footR.left - footR.right) / footR.total; 
    } else { footR.copX = footR.copY = 0; }
}