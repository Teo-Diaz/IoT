#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "../common/ControlProtocol.h"
#include "LoRaBoards.h"

// ---------- Board selection: LilyGO T-Beam (ESP32) ----------
#if !defined(ESP32)
#error "This TX build targets the LilyGO T-Beam (ESP32)."
#endif

#ifndef CONFIG_RADIO_FREQ
#define CONFIG_RADIO_FREQ 910.0
#endif
#ifndef CONFIG_RADIO_OUTPUT_POWER
#define CONFIG_RADIO_OUTPUT_POWER 17
#endif
#ifndef CONFIG_RADIO_BW
#define CONFIG_RADIO_BW 125.0
#endif

// ========================================
// MODO DE OPERACIÓN
// 1 = AP + Web UI
// 2 = Cliente WiFi + GET a servidor
// ========================================
const uint8_t MODE = 2;

const char *kStaSsid = "UPBWiFi";
const char *kStaPassword = "";

const char *kServerUrl = "http://3.230.70.191:4040/status";

uint8_t sequenceCounter = 0;
uint8_t currentLeftSpeed = 0;
uint8_t currentRightSpeed = 0;
String lastState = "STOP";

unsigned long lastGetTime = 0;
const long getInterval = 500;
unsigned long lastWifiCheck = 0;
const long wifiCheckInterval = 5000;

bool wasConnected = false;
bool lastCommandWasStop = true;

WebServer server(80);

const char indexPage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Tank Controller TX</title>
  <style>
    body { font-family: sans-serif; margin: 0; padding: 2rem; background: #101820; color: #eee; }
    h1 { margin-top: 0; }
    button { width: 8rem; height: 3rem; margin: 0.5rem; font-size: 1rem; border: none; border-radius: 0.5rem; cursor: pointer; background: #ff7a18; color: #101820; }
    button.stop { background: #ff3b30; color: #fff; }
    #status { margin-top: 1.5rem; font-size: 1.1rem; }
    .pad { display: grid; grid-template-columns: repeat(3, 8.5rem); grid-template-rows: repeat(3, 3.5rem); gap: 0.5rem; justify-content: center; margin-top: 2rem; }
    .pad button { width: 100%; height: 100%; }
    .speeds { margin-top: 2rem; display: flex; gap: 1.5rem; justify-content: center; }
    .speeds label { display: flex; flex-direction: column; align-items: center; font-size: 0.9rem; }
    input[type=range] { width: 200px; }
    footer { margin-top: 3rem; font-size: 0.85rem; color: #aaa; text-align: center; }
  </style>
</head>
<body>
  <h1>T-Beam Tank Controller</h1>
  <p>Tap a button to send a command over LoRa. Commands are AES-256 encrypted.</p>
  <div class="pad">
    <div></div>
    <button data-cmd="forward">Forward</button>
    <div></div>
    <button data-cmd="left">Left</button>
    <button class="stop" data-cmd="stop">Stop</button>
    <button data-cmd="right">Right</button>
    <div></div>
    <button data-cmd="backward">Backward</button>
    <div></div>
  </div>
  <div class="speeds">
    <label>Left speed
      <input id="leftSpeed" type="range" min="0" max="255" value="255">
      <span id="leftValue">255</span>
    </label>
    <label>Right speed
      <input id="rightSpeed" type="range" min="0" max="255" value="255">
      <span id="rightValue">255</span>
    </label>
    <button data-cmd="speed" id="speedBtn">Set Speeds</button>
  </div>
  <div id="status">State: IDLE</div>
  <footer>Connect to the TankController Wi-Fi network (password: tank12345).</footer>
  <script>
    const statusEl = document.getElementById('status');
    const left = document.getElementById('leftSpeed');
    const right = document.getElementById('rightSpeed');
    const leftValue = document.getElementById('leftValue');
    const rightValue = document.getElementById('rightValue');

    function updateLabels() {
      leftValue.textContent = left.value;
      rightValue.textContent = right.value;
    }
    left.addEventListener('input', updateLabels);
    right.addEventListener('input', updateLabels);
    updateLabels();

    async function sendCommand(cmd) {
      statusEl.textContent = 'State: sending...';
      const params = new URLSearchParams({ action: cmd });
      if (cmd === 'speed') {
        params.set('left', left.value);
        params.set('right', right.value);
      }
      try {
        const res = await fetch('/cmd', { method: 'POST', body: params });
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        statusEl.textContent = `State: ${data.state}`;
      } catch (err) {
        statusEl.textContent = 'State: ERROR - ' + err.message;
      }
    }

    document.querySelectorAll('button[data-cmd]').forEach(btn => {
      btn.addEventListener('click', () => sendCommand(btn.dataset.cmd));
    });
    document.getElementById('speedBtn').addEventListener('click', () => sendCommand('speed'));
  </script>
</body>
</html>
)rawliteral";

TankControl::Command parseCommand(const String &action)
{
  if (action == "forward")
    return TankControl::Command::Forward;
  if (action == "backward")
    return TankControl::Command::Backward;
  if (action == "left")
    return TankControl::Command::Left;
  if (action == "right")
    return TankControl::Command::Right;
  if (action == "speed")
    return TankControl::Command::SetSpeed;
  return TankControl::Command::Stop;
}

bool sendLoRaFrame(TankControl::Command cmd, uint8_t leftSpeed, uint8_t rightSpeed)
{
  TankControl::ControlFrame frame;
  TankControl::initFrame(frame, cmd, leftSpeed, rightSpeed, sequenceCounter++);

  uint8_t encrypted[TankControl::kFrameSize];
  if (!TankControl::encryptFrame(frame, encrypted, sizeof(encrypted)))
  {
    Serial.println("Encrypt failed");
    return false;
  }

  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(encrypted, sizeof(encrypted));
  bool ok = LoRa.endPacket() == 1;
  LoRa.receive();

  if (ok)
  {
    Serial.print("TX -> cmd=");
    Serial.print(static_cast<int>(frame.command));
    Serial.print(" seq=");
    Serial.print(frame.sequence);
    Serial.print(" left=");
    Serial.print(frame.leftSpeed);
    Serial.print(" right=");
    Serial.println(frame.rightSpeed);
  }
  else
  {
    Serial.println("LoRa TX failed");
  }
  return ok;
}

void sendStopCommand()
{
  if (lastCommandWasStop && currentLeftSpeed == 0 && currentRightSpeed == 0)
  {
    return;
  }

  currentLeftSpeed = 0;
  currentRightSpeed = 0;
  lastState = "STOP";
  lastCommandWasStop = true;

  if (sendLoRaFrame(TankControl::Command::Stop, 0, 0))
  {
    Serial.println("SAFETY STOP sent: No connection or server error.");
  }
}

void sendSpectrumTestBurst()
{
  static constexpr size_t kBurstSize = 192;
  uint8_t payload[kBurstSize];
  for (size_t i = 0; i < kBurstSize; ++i)
  {
    payload[i] = static_cast<uint8_t>(random(0, 256));
  }

  Serial.println("Sending LoRa spectrum test burst...");
  LoRa.idle();
  LoRa.beginPacket();
  LoRa.write(payload, sizeof(payload));
  if (LoRa.endPacket() == 1)
  {
    Serial.print("Burst length: ");
    Serial.println(sizeof(payload));
  }
  else
  {
    Serial.println("Spectrum test burst failed to transmit");
  }
  LoRa.receive();
}

bool beginLoRa()
{
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN, RADIO_CS_PIN);
  LoRa.setPins(RADIO_CS_PIN, RADIO_RST_PIN, RADIO_DIO0_PIN);

#ifdef RADIO_TCXO_ENABLE
  pinMode(RADIO_TCXO_ENABLE, OUTPUT);
  digitalWrite(RADIO_TCXO_ENABLE, HIGH);
#endif

  if (!LoRa.begin(CONFIG_RADIO_FREQ * 1000000))
  {
    Serial.println("LoRa init failed. Check wiring.");
    return false;
  }

  LoRa.setTxPower(CONFIG_RADIO_OUTPUT_POWER);
  LoRa.setSignalBandwidth(CONFIG_RADIO_BW * 1000);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.receive();

  Serial.println("LoRa radio ready (TX).");
  return true;
}

void handleWebRoot()
{
  server.send_P(200, "text/html", indexPage);
}

void handleWebCommand()
{
  if (!server.hasArg("action"))
  {
    server.send(400, "application/json", "{\"error\":\"missing action\"}");
    return;
  }

  String action = server.arg("action");
  action.toLowerCase();
  TankControl::Command cmd = parseCommand(action);
  bool ok = true;

  if (cmd == TankControl::Command::SetSpeed)
  {
    int left = server.hasArg("left") ? server.arg("left").toInt() : currentLeftSpeed;
    int right = server.hasArg("right") ? server.arg("right").toInt() : currentRightSpeed;
    left = constrain(left, 0, 255);
    right = constrain(right, 0, 255);
    currentLeftSpeed = static_cast<uint8_t>(left);
    currentRightSpeed = static_cast<uint8_t>(right);
    ok = sendLoRaFrame(cmd, currentLeftSpeed, currentRightSpeed);
    if (ok)
      lastState = "SPEED";
  }
  else
  {
    ok = sendLoRaFrame(cmd, currentLeftSpeed, currentRightSpeed);
    if (ok)
    {
      switch (cmd)
      {
      case TankControl::Command::Forward:
        lastState = "FORWARD";
        break;
      case TankControl::Command::Backward:
        lastState = "BACKWARD";
        break;
      case TankControl::Command::Left:
        lastState = "LEFT";
        break;
      case TankControl::Command::Right:
        lastState = "RIGHT";
        break;
      case TankControl::Command::Stop:
        lastState = "STOP";
        break;
      default:
        break;
      }
    }
  }

  if (!ok)
  {
    server.send(500, "application/json", "{\"error\":\"lora tx failed\"}");
    return;
  }

  String body = "{\"state\":\"";
  body += lastState;
  body += "\"}";
  server.send(200, "application/json", body);
}

void performHttpGet()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    sendStopCommand();
    return;
  }

  HTTPClient http;
  http.setTimeout(2000);
  http.begin(kServerUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    String payload = http.getString();

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error)
    {
      Serial.println("JSON parse failed: " + String(error.c_str()));
      sendStopCommand();
      http.end();
      return;
    }

    const char *cmdStr = doc["command"] | "STOP";
    int speedness = doc["speedness"] | 0;

    speedness = constrain(speedness, 0, 100);
    uint8_t speed = map(speedness, 0, 100, 0, 255);

    TankControl::Command cmd = TankControl::Command::Stop;
    if (strcmp(cmdStr, "FORWARD") == 0)
      cmd = TankControl::Command::Forward;
    else if (strcmp(cmdStr, "BACKWARD") == 0)
      cmd = TankControl::Command::Backward;
    else if (strcmp(cmdStr, "LEFT") == 0)
      cmd = TankControl::Command::Left;
    else if (strcmp(cmdStr, "RIGHT") == 0)
      cmd = TankControl::Command::Right;
    else if (strcmp(cmdStr, "STOP") == 0)
      cmd = TankControl::Command::Stop;

    currentLeftSpeed = speed;
    currentRightSpeed = speed;
    lastState = cmdStr;
    lastCommandWasStop = (cmd == TankControl::Command::Stop);

    if (sendLoRaFrame(cmd, speed, speed))
    {
      Serial.println("From server: " + String(cmdStr) + " @ " + String(speedness) + "%");
    }
  }
  else
  {
    Serial.printf("HTTP failed: %d\n", httpCode);
    sendStopCommand();
  }
  http.end();
}

// === SETUP ===
void setup()
{
  setupBoards(/*disable_u8g2=*/true);
  delay(1500);
  while (!Serial)
  {
    delay(10);
  }

  Serial.println("\nT-Beam TX | LoRa Tank Controller");

  bool radioReady = beginLoRa();
  if (!radioReady)
  {
    Serial.println("LoRa setup failed; reboot after checking the radio module.");
    while (true)
      delay(1000);
  }
  else
  {
    randomSeed(esp_random());
    sendSpectrumTestBurst();
  }

  sendStopCommand();

  if (MODE == 1)
  {
    Serial.println("Starting in AP + Web UI mode (MODE 1)");
    WiFi.mode(WIFI_AP);
    if (WiFi.softAP("TankController", "tank12345"))
    {
      Serial.print("SoftAP ready. SSID: TankController, IP: ");
      Serial.println(WiFi.softAPIP());
    }
    else
    {
      Serial.println("Failed to start SoftAP.");
    }

    server.on("/", HTTP_GET, handleWebRoot);
    server.on("/cmd", HTTP_POST, handleWebCommand);
    server.onNotFound([]()
                      { server.send(404, "application/json", "{\"error\":\"not found\"}"); });
    server.begin();
    Serial.println("Web UI ready at http://" + WiFi.softAPIP().toString());
  }
  else if (MODE == 2)
  {
    Serial.println("Starting in WiFi Client + Server GET mode (MODE 2)");
    WiFi.mode(WIFI_STA);
    WiFi.begin(kStaSsid, kStaPassword);

    Serial.print("Connecting to ");
    Serial.println(kStaSsid);

    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
    {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("\nWiFi connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
    else
    {
      Serial.println("\nFailed to connect to WiFi. Will retry in loop.");
      sendStopCommand();
    }
  }
  else
  {
    Serial.println("Invalid MODE. Must be 1 or 2.");
    while (true)
      delay(1000);
  }
}

// === LOOP ===
void loop()
{
  if (MODE == 1)
  {
    server.handleClient();
  }
  else if (MODE == 2)
  {
    unsigned long now = millis();

    if (now - lastWifiCheck >= wifiCheckInterval)
    {
      lastWifiCheck = now;
      bool currentlyConnected = (WiFi.status() == WL_CONNECTED);

      if (wasConnected && !currentlyConnected)
      {
        Serial.println("WiFi LOST -> Sending STOP");
        sendStopCommand();
      }
      else if (!wasConnected && currentlyConnected)
      {
        Serial.println("WiFi RECONNECTED");
      }
      wasConnected = currentlyConnected;

      if (!currentlyConnected)
      {
        Serial.println("Attempting WiFi reconnect...");
        WiFi.reconnect();
      }
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      if (now - lastGetTime >= getInterval)
      {
        lastGetTime = now;
        performHttpGet();
      }
    }
    else
    {
      static unsigned long lastSafetyStop = 0;
      if (now - lastSafetyStop >= 1000)
      {
        lastSafetyStop = now;
        sendStopCommand();
      }
    }
  }
}