#pragma once

#include <optional>

#include <WiFi.h>

#include <esp_mac.h>
#include <esp_now.h>
#include <esp_wifi.h>

uint8_t remote_mac[6] = {0xF0, 0x24, 0xF9, 0x54, 0x0A, 0xB4};
uint8_t sign_mac[6] = {0xF0, 0x24, 0xF9, 0xE8, 0x35, 0xE0};

inline std::optional<String> get_command(const esp_now_recv_info_t* recv_info, const unsigned char* incomingData, int len) {
  char msg[256];
  memcpy(msg, incomingData, len);
  msg[len] = 0;
  Serial.print("Received: ");
  Serial.println(msg);

  String str_msg = String(msg);
  if (str_msg.startsWith("CMD ")) {
    String cmd = str_msg.substring(4);
    return cmd;
  }
  return std::nullopt;
}

inline bool setup_esp_now(uint8_t peer_mac[6], esp_now_recv_cb_t on_command) {
  Serial.println("Setting up ESP-NOW...");

  Serial.println("Setting up ESP-NOW: Enable WIFI_STA");
  WiFi.mode(WIFI_STA);   // required for ESP-NOW

  Serial.println("Setting up ESP-NOW: Initialize ESP-NOW");
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return false;
  }

  // Register callbacks
  Serial.println("Setting up ESP-NOW: Register Callbacks");
  esp_now_register_send_cb([](const wifi_tx_info_t* tx_info, esp_now_send_status_t status) {
      Serial.print("Last Packet Send Status: ");
      Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  });
  esp_now_register_recv_cb(on_command);

  Serial.println("Setting up ESP-NOW: Add Peer");
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peer_mac, 6);
  peerInfo.channel = 0;  // use the current channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Setting up ESP-NOW: Failed to add peer!!!");
    return false;
  }
  else {
    Serial.println("Setting up ESP-NOW: Added peer.");
  }

  Serial.println("Setting up ESP-NOW: Complete");
  return true;
}

void teardown_esp_now() {
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();

  if (esp_now_deinit() != ESP_OK) {
    Serial.println("Error deinitializing ESP-NOW");
  }

  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
}

inline bool send_command(uint8_t mac[6], String cmd) {
  Serial.print("Sending command: ");
  Serial.println(cmd);

  String decorated_cmd = "CMD " + cmd;

  esp_err_t result = esp_now_send(mac, (uint8_t *)decorated_cmd.c_str(), decorated_cmd.length());
  if (result != ESP_OK) {
    Serial.println("Error sending the data");    
    return false;
  }

  Serial.println("Sent with success");
  return true;
}

// Get STA MAC as bytes
inline void getStaMac(uint8_t out[6]) {
  // Ensure STA interface is initialized so the MAC is set
  WiFi.mode(WIFI_STA);              // does not connect; just readies STA iface
  esp_read_mac(out, ESP_MAC_WIFI_STA);
}

// Pretty-print a MAC
inline String macToString(const uint8_t mac[6]) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}