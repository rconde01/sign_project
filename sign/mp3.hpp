#pragma once

#include <Arduino.h>

// Helper to send a 6-byte frame: 7E, CMD, 00, 02, DH, DL, EF
void mp3SendCmd(HardwareSerial &mp3, uint8_t cmd, uint8_t DH, uint8_t DL) {
  uint8_t frame[7] = {0x7E, cmd, 0x00, 0x02, DH, DL, 0xEF};
  mp3.write(frame, sizeof(frame));
  mp3.flush();
}

void mp3Play(HardwareSerial &mp3) {
  mp3SendCmd(mp3, 0x0D, 0x00, 0x00);
}

void mp3Pause(HardwareSerial &mp3) {
  mp3SendCmd(mp3, 0x0E, 0x00, 0x00);
}

void mp3Stop(HardwareSerial &mp3) {
  mp3SendCmd(mp3, 0x16, 0x00, 0x00);
}

void mp3SetVolume(HardwareSerial &mp3, uint8_t level) {
  if (level > 30) level = 30;
  // 0x06 = set volume; DH=0x00 (no memory), DL=level (0..30)
  mp3SendCmd(mp3, 0x06, 0x00, level);
}

void mp3PlayIndex(HardwareSerial &mp3, uint16_t index /*1..2999*/) {
  Serial.printf("playing index %d\n",static_cast<uint32_t>(index));

  uint8_t hi = (index >> 8) & 0xFF;
  uint8_t lo = index & 0xFF;
  // 0x03 = play track by global index in ROOT
  mp3SendCmd(mp3, 0x03, hi, lo);
}