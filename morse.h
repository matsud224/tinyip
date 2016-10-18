#pragma once

#define DOT_UNIT 200

#define PTN_FALL 0x01
#define PTN_RISE 0x02

#define CHAR_SEND 0x03
#define CHAR_CLEAR 0x04

void start_morse();
char morse_getc();
