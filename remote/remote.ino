#include <cmath>
#include <array>

#include "action.hpp"
#include "../common/communication.hpp"
#include "../common/atomic.hpp"

#define BTN_SOPHIA 27
#define BTN_COFFEE 33
#define BTN_DOG 14
#define BTN_CAT 32

#define BACKLIGHT1_RED 22
#define BACKLIGHT1_GREEN 25
#define BACKLIGHT1_BLUE 26
#define BACKLIGHT2_RED 23
#define BACKLIGHT2_GREEN 5
#define BACKLIGHT2_BLUE 4
#define BACKLIGHT3_RED 16
#define BACKLIGHT3_GREEN 21
#define BACKLIGHT3_BLUE 17

#define LEDC_FREQ            5000    // 5 kHz PWM
#define LEDC_RESOLUTION      8       // 8-bit (0-255)

// Linger time on the REMOTE after a press (ms)
static const uint32_t REMOTE_LINGER_MS = 120000;  // 2 minutes
// If no data at all for this long, the REMOTE closes early
static const uint32_t REMOTE_INACTIVITY_MS = 120000; // 2 minutes

std::array<int,4> BTN_PINS = {BTN_SOPHIA, BTN_COFFEE, BTN_DOG, BTN_CAT};

struct Color {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

auto const yellow       = Color{255,255,0};
auto const blue         = Color{0,0,255};
auto const purple       = Color{255,0,255};
auto const orange       = Color{255,165,0};
auto const red          = Color{255,0,0};
auto const green        = Color{0,255,0};

struct PulseData {
  Color color{};
};

// Connection lifecycle (Wi-Fi brought up only in these states)
enum class RState { IDLE, WIFI_UP, RESOLVING, CONNECTING, CONNECTED, LINGER, SHUTDOWN };

struct Data {
  PulseData       pulse_data{};
  MyAtomic<bool>  is_pulsing{false};
  TaskHandle_t    send_message_task_handle{};
  TaskHandle_t    pulse_color_task_handle{};
  
  WiFiClient      client{};
  IPAddress       signIP{};
  bool            mdnsReady{false};
  uint32_t        connectedAt{0};
  uint32_t        lastRx{0};
  uint32_t        lastPing{0};
  RState          state{RState::IDLE};
  bool            wifiIsOn{false};
  int             active_command_button_index{-1};
  std::array<uint32_t,BTN_PINS.size()> lastEdge{0,0,0,0};
  std::array<uint32_t,BTN_PINS.size()> lastState{0,0,0,0};
};

void wifiOn(Data & data){
  if (data.wifiIsOn)
    return;

  logLine("WiFi","ON");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);               // low latency for quick transactions
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  data.wifiIsOn = true;
  data.mdnsReady = false;
}

void wifiOff(Data & data){
  if (!data.wifiIsOn)
    return;

  logLine("WiFi","OFF");
  WiFi.disconnect(true, true);        // drop & erase old config
  WiFi.mode(WIFI_OFF);
  data.wifiIsOn = false;
  data.mdnsReady = false;
  data.signIP = IPAddress();
}

void onWiFiEvent(Data & data, WiFiEvent_t e){
  if (e == ARDUINO_EVENT_WIFI_STA_GOT_IP){
    logLine("WiFi","GOT_IP " + WiFi.localIP().toString());

    // (Re)start mDNS each time we come up
    if (MDNS.begin((String("remote-") + deviceId()).c_str())){
      data.mdnsReady = true;
      logLine("MDNS","ready");
    } else {
      data.mdnsReady = false;
      logLine("MDNS","begin failed");
    }
  } else if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED){
    logLine("WiFi","DISCONNECTED");
    data.mdnsReady = false;
  }
}

bool mdnsResolveSign(Data & data){
  logLine("MDNS","Resolving " + String(SIGN_HOSTNAME));

  if (!data.mdnsReady)
    return false;

  IPAddress ip = MDNS.queryHost(SIGN_HOSTNAME);

  if (ip){
    data.signIP = ip;
    logLine("MDNS","sign.local -> "+ip.toString());
    return true;
  }

  logLine("MDNS","resolve failed");

  return false;
}

void startTransaction(Data & data, int btnIndex){
  // Bring Wi-Fi up fresh each time
  wifiOn(data);
  data.state = RState::WIFI_UP;
}

void setup_buttons(){
  for(auto pin : BTN_PINS)
    pinMode(pin, INPUT_PULLUP);
}

void setup_leds(){
  ledcSetClockSource(LEDC_AUTO_CLK);

  ledcAttachChannel(BACKLIGHT1_RED,   LEDC_FREQ, LEDC_RESOLUTION, 0);
  ledcAttachChannel(BACKLIGHT1_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 1);
  ledcAttachChannel(BACKLIGHT1_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 2);
  ledcAttachChannel(BACKLIGHT2_RED,   LEDC_FREQ, LEDC_RESOLUTION, 3);
  ledcAttachChannel(BACKLIGHT2_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 4);
  ledcAttachChannel(BACKLIGHT2_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 5);
  ledcAttachChannel(BACKLIGHT3_RED,   LEDC_FREQ, LEDC_RESOLUTION, 6);
  ledcAttachChannel(BACKLIGHT3_GREEN, LEDC_FREQ, LEDC_RESOLUTION, 7);
  ledcAttachChannel(BACKLIGHT3_BLUE,  LEDC_FREQ, LEDC_RESOLUTION, 8);

  // It's weird but without this the writes below to shut off the leds don't work as expected
  // Some shut off and some don't
  delay(100);

  ledcWrite(BACKLIGHT1_RED,   255);
  ledcWrite(BACKLIGHT1_GREEN, 255);
  ledcWrite(BACKLIGHT1_BLUE,  255);
  ledcWrite(BACKLIGHT2_RED,   255);
  ledcWrite(BACKLIGHT2_GREEN, 255);
  ledcWrite(BACKLIGHT2_BLUE,  255);
  ledcWrite(BACKLIGHT3_RED,   255);
  ledcWrite(BACKLIGHT3_GREEN, 255);
  ledcWrite(BACKLIGHT3_BLUE,  255);
}

Color get_button_color(int button){
  switch(button){
  case 0:
    return purple;
  case 1:
    return orange;
  case 2:
    return blue;
  case 3:
    return yellow;
  default:
    return {0,0,0};
  }
}

void pulse_color(Data & data, Color color){
  float rf = color.r / 255.0;
  float gf = color.g / 255.0;
  float bf = color.b / 255.0;

  int num_steps = 256;
  int delay_value = 2;

  for(int i = 0; i < num_steps; ++i){
    float step = 1.0f - (float)i / (float)(num_steps - 1);
    int red = 255 - (int)(rf*step*255);
    int green = 255 - (int)(gf*step*255);
    int blue = 255 - (int)(bf*step*255);

    ledcWrite(BACKLIGHT1_RED,   red);
    ledcWrite(BACKLIGHT1_GREEN, green);
    ledcWrite(BACKLIGHT1_BLUE,  blue);
    ledcWrite(BACKLIGHT2_RED,   red);
    ledcWrite(BACKLIGHT2_GREEN, green);
    ledcWrite(BACKLIGHT2_BLUE,  blue);
    ledcWrite(BACKLIGHT3_RED,   red);
    ledcWrite(BACKLIGHT3_GREEN, green);
    ledcWrite(BACKLIGHT3_BLUE,  blue);
    vTaskDelay(pdMS_TO_TICKS(delay_value));
  }

  data.is_pulsing = false;
}

void long_pulse_color_blocking(Color color){
  int num_steps = 1024;
  int delay_value = 2;
  int num_cycles = 3;

  for(int i = 0; i < num_steps; ++i){
    float step = 1.0f - (float)i / (float)(num_steps - 1);

    auto level = 1.0f - (1.0f + cosf(num_cycles*step*2*M_PI))/2.0;

    int red = 255 - (int)(color.r*level);
    int green = 255 - (int)(color.g*level);
    int blue = 255 - (int)(color.b*level);

    ledcWrite(BACKLIGHT1_RED,   red);
    ledcWrite(BACKLIGHT1_GREEN, green);
    ledcWrite(BACKLIGHT1_BLUE,  blue);
    ledcWrite(BACKLIGHT2_RED,   red);
    ledcWrite(BACKLIGHT2_GREEN, green);
    ledcWrite(BACKLIGHT2_BLUE,  blue);
    ledcWrite(BACKLIGHT3_RED,   red);
    ledcWrite(BACKLIGHT3_GREEN, green);
    ledcWrite(BACKLIGHT3_BLUE,  blue);
    delay(delay_value);
  }
}

void pulse_color_task(void * data){
  auto the_data = reinterpret_cast<Data *>(data);

  pulse_color(*the_data, the_data->pulse_data.color);

  vTaskDelete(NULL);
}

void create_pulse_task(Data & data, Color color){
  data.pulse_data.color = color;

  xTaskCreate(
    pulse_color_task,
    "pulse task",
    2048,
    &data,
    1,
    &data.pulse_color_task_handle);
}

void handle_button_press(Data & data, int button_index){
  logLine("BTN","Button Pressed");

  if(data.active_command_button_index != -1)
    return; // already in a transaction

  data.is_pulsing = true;
  auto color = get_button_color(button_index);
  create_pulse_task(data, color);

  data.active_command_button_index = button_index;
  startTransaction(data, button_index);
}

void send_button_command(Data & data, int button_index){
  if (data.state != RState::CONNECTED)
    return;

  if (button_index < 0 || button_index >= BTN_PINS.size())
    return;

  String cmd;
  switch(button_index){
  case 0:
    cmd = "coffee";
    break;
  case 1:
    cmd = "sophia";
    break;
  case 2:
    cmd = "dog";
    break;
  case 3:
    cmd = "cat";
    break;
  default:
    return;
  }

  if (sendLine(data.client, String("CMD ") + cmd)){
    logLine("TCP","sent CMD " + cmd);
  } else {
    logLine("TCP","send failed");
    data.client.stop();
    data.state = RState::SHUTDOWN;
  }
}

void pollButtons(Data & data){
  uint32_t now = millis();

  for (int i = 0; i < BTN_PINS.size(); i++){
    auto s = digitalRead(BTN_PINS[i]) == LOW;

    if (s != data.lastState[i] && (now - data.lastEdge[i]) > DEBOUNCE_MS){
      data.lastEdge[i] = now;
      data.lastState[i] = s;

      if (s){ // pressed
        handle_button_press(data, i);
        return;
      }
    }
  }
}

void setup_wifi(Data & data){
  WiFi.onEvent([&data](WiFiEvent_t e, WiFiEventInfo_t info) { onWiFiEvent(data, e); });
  WiFi.setAutoReconnect(true);
}

Data g_data{};

void setup(){
  Serial.begin(115200);
  delay(2000);

  logLine("BOOT","REMOTE id="+deviceId());

  setup_wifi(g_data);
  setup_buttons();
  setup_leds();

  // Start with Wi-Fi fully OFF
  wifiOff(g_data);
}

void loop(){
  // 1) Idle: Wi-Fi OFF; just watch buttons
  if (g_data.state == RState::IDLE || g_data.state == RState::SHUTDOWN){
    pollButtons(g_data);
    delay(2);
    return;
  }

  // State progression
  switch (g_data.state){
    case RState::WIFI_UP:
      if (WiFi.status() == WL_CONNECTED){
        g_data.state = RState::RESOLVING;
        logLine("STATE","Wifi UP : Enter Resolving");
      }
      break;

    case RState::RESOLVING:
      if (mdnsResolveSign(g_data)) {
        g_data.state = RState::CONNECTING;
        logLine("STATE","Resolving : Enter Connecting");
      }
      else {
        // could retry a couple times here; for simplicity, abort
        g_data.state = RState::SHUTDOWN;
        logLine("STATE","Resolving : Enter Shutdown");
      }
      break;

    case RState::CONNECTING:
      if (!g_data.signIP){
        g_data.state = RState::SHUTDOWN;
        logLine("STATE","Connecting : Enter Shutdown");
        break;
      }

      logLine("TCP","connect " + g_data.signIP.toString() + ":" + String(TCP_PORT));

      if (g_data.client.connect(g_data.signIP, TCP_PORT, 2000)){
        g_data.client.setNoDelay(true);
        g_data.connectedAt = g_data.lastRx = millis();
        g_data.lastPing = 0;
        logLine("TCP","connected");
        g_data.state = RState::CONNECTED;
        logLine("STATE","Connected : Enter Connected");

        // Send the initial command if we have one
        if (g_data.active_command_button_index >= 0){
          send_button_command(g_data, g_data.active_command_button_index);
          g_data.active_command_button_index = -1;
        }
      } else {
        logLine("TCP","connect failed");
        g_data.client.stop();
        g_data.state = RState::SHUTDOWN;
        logLine("STATE","Connected : Enter Shutdown");
      }
      break;

    case RState::CONNECTED:
      // Read lines; process; then switch to LINGER
      if (g_data.client.connected()){
        String line;

        while (readLine(g_data.client, line)){
          g_data.lastRx = millis();

          if(line.startsWith("CMD ")){
            // TODO execute command
          }

          line = "";
        }

        // Heartbeat during linger window (even before we flip to LINGER, harmless)
        if (millis() - g_data.lastPing >= HEARTBEAT_MS){
          g_data.lastPing = millis();
          sendLine(g_data.client, String("PING ") + g_data.lastPing);
        }

        // Enter linger state immediately after connection & first send
        g_data.state = RState::LINGER;
      } else {
        g_data.state = RState::SHUTDOWN;
      }
      break;

    case RState::LINGER:
      {
        if (!g_data.client.connected()) {
          g_data.state = RState::SHUTDOWN;
          logLine("STATE","Linger : Enter Shutdown");
          break;
        }

        String line;

        while (readLine(g_data.client, line)){
          g_data.lastRx = millis();
          line = "";
        }

        // Keepalive
        if (millis() - g_data.lastPing >= HEARTBEAT_MS){
          g_data.lastPing = millis();
          sendLine(g_data.client, String("PING ") + g_data.lastPing);
        }

        // Early close if totally quiet
        if (millis() - g_data.lastRx >= REMOTE_INACTIVITY_MS){
          logLine("LINK","no server traffic -> close");
          g_data.client.stop();
          g_data.state = RState::SHUTDOWN;
          logLine("STATE","Linger : Enter Shutdown");          
          break;
        }

        // Hard stop at linger limit
        if (millis() - g_data.connectedAt >= REMOTE_LINGER_MS){
          logLine("TCP","linger elapsed -> close");
          g_data.client.stop();
          g_data.state = RState::SHUTDOWN;
          logLine("STATE","Linger : Enter Shutdown");          
          break;
        }
      }
      break;

    case RState::SHUTDOWN:
      // Clean down: close TCP, power off Wi-Fi
      if (g_data.client.connected())
        g_data.client.stop();

      wifiOff(g_data);
      g_data.state = RState::IDLE;
      logLine("STATE","Shutdown : Enter Idle");
      break;

    default:
      break;
  }

  delay(2);
}