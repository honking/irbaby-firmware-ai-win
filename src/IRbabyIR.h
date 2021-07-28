#ifndef IRBABYIR_H
#define IRBABYIR_H

#include <Arduino.h>
#include <IRsend.h>
#include <IRrecv.h>
#include "../lib/Irext/include/ir_decode.h"

void loadIRPin(uint8_t send_pin, uint8_t recv_pin);
void enableIR();
void disableIR();
void sendStatus(String file_name, t_remote_ac_status status);
bool sendKey(String file_name, int key, const UINT8 category);
bool sendIR(String file_name);
void recvIR();
bool saveIR(String file_name);
void initAC(String);
void sendControl(String file, const UINT8 category, UINT8 key_code, t_remote_ac_status* ac_status);

#endif // IRBABAYIR_H