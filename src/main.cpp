#include <Arduino.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Bridge.h>
#include <Process.h>
#include <Wire.h>

#include "config.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

constexpr uint8_t TRIG_PIN = 9;
constexpr uint8_t ECHO_PIN = 8;
constexpr uint8_t LEFT_PIN = 11;
constexpr uint8_t SELECT_PIN = 12;
constexpr uint8_t RIGHT_PIN = 13;
constexpr unsigned long STARTUP_WINDOW_MS = 5000;
constexpr unsigned long DEBOUNCE_MS = 35;
constexpr unsigned long SELECT_HOLD_MS = 900;
constexpr unsigned long SENSOR_INTERVAL_MS = 80;

enum class AppState : uint8_t {
  Startup,
  ProfileSelect,
  ApiError,
  CalibrateStanding,
  CalibrateLean,
  Counting
};

struct Button {
  uint8_t pin;
  bool stablePressed = false;
  bool lastReading = false;
  bool longPressSent = false;
  unsigned long lastChangeMs = 0;
  unsigned long pressedAtMs = 0;
};

struct ButtonEvents {
  bool pressed = false;
  bool released = false;
  bool shortPress = false;
  bool longPress = false;
};

struct Profile {
  String id;
  String name;
  float minCm = 0.0f;
  float maxCm = 0.0f;
};

Button leftButton{LEFT_PIN};
Button selectButton{SELECT_PIN};
Button rightButton{RIGHT_PIN};
AppState appState = AppState::Startup;
Profile profiles[PROFILE_LIMIT];
size_t profileCount = 0;
size_t selectedProfile = 0;
Profile activeProfile;
bool offlineMode = false;
bool repInProgress = false;
bool retryProfileSave = false;
unsigned int repCount = 0;
float standingDistanceCm = -1.0f;
float leanDistanceCm = -1.0f;
unsigned long startupAtMs = 0;
unsigned long lastSensorAtMs = 0;
unsigned long lastDisplayAtMs = 0;
int lastStartupSeconds = -1;

ButtonEvents updateButton(Button &button, unsigned long now) {
  ButtonEvents events;
  const bool reading = digitalRead(button.pin) == HIGH;

  if (reading != button.lastReading) {
    button.lastReading = reading;
    button.lastChangeMs = now;
  }

  if (reading != button.stablePressed && now - button.lastChangeMs >= DEBOUNCE_MS) {
    button.stablePressed = reading;
    if (reading) {
      button.pressedAtMs = now;
      button.longPressSent = false;
      events.pressed = true;
    } else {
      events.released = true;
      events.shortPress = !button.longPressSent;
    }
  }

  if (button.stablePressed && !button.longPressSent && now - button.pressedAtMs >= SELECT_HOLD_MS) {
    button.longPressSent = true;
    events.longPress = true;
  }
  return events;
}

void drawScreen(const __FlashStringHelper *line1,
                const __FlashStringHelper *line2 = nullptr,
                const __FlashStringHelper *line3 = nullptr,
                const __FlashStringHelper *line4 = nullptr) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  if (line1) display.println(line1);
  display.setCursor(0, 16);
  if (line2) display.println(line2);
  display.setCursor(0, 28);
  if (line3) display.println(line3);
  display.setCursor(0, 48);
  if (line4) display.println(line4);
  display.display();
}

void showStartup(uint8_t secondsRemaining) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.println(F("WALL PUSH-UPS"));
  display.setCursor(0, 17);
  display.println(F("Any button: OFFLINE"));
  display.setCursor(0, 29);
  display.println(F("No press: connect"));
  display.setCursor(0, 51);
  display.print(F("Starting in "));
  display.print(secondsRemaining);
  display.println(F("s"));
  display.display();
}

void showProfileSelect() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  display.println(F("SELECT PROFILE"));
  if (profileCount == 0) {
    display.setCursor(0, 19);
    display.println(F("No saved profiles"));
    display.setCursor(0, 39);
    display.println(F("Hold SELECT to"));
    display.setCursor(0, 51);
    display.println(F("create one"));
  } else {
    display.setCursor(0, 13);
    display.print(selectedProfile + 1);
    display.print(F(" OF "));
    display.println(profileCount);

    String name = profiles[selectedProfile].name;
    if (name.length() > 10) name = name.substring(0, 9) + ".";
    display.setTextSize(2);
    display.setCursor(0, 25);
    display.println(name);
    display.setTextSize(1);
    display.setCursor(0, 47);
    display.println(F("< PREV    NEXT >"));
    display.setCursor(0, 56);
    display.println(F("SEL:LOAD HOLD:NEW"));
  }
  display.display();
}

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
  if (duration == 0) return -1.0f;
  return static_cast<float>(duration) * 0.0343f / 2.0f;
}

float captureDistanceCm() {
  float total = 0.0f;
  uint8_t samples = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    const float distance = readDistanceCm();
    if (distance > 0.0f) {
      total += distance;
      ++samples;
    }
    delay(70);
  }
  return samples >= 5 ? total / samples : -1.0f;
}

bool requestApi(const String &method, const String &path, const String &body, String &response) {
  Process process;
  process.begin(F("curl"));
  process.addParameter(F("--silent"));
  process.addParameter(F("--show-error"));
  process.addParameter(F("--fail"));
  process.addParameter(F("--max-time"));
  process.addParameter(String(API_TIMEOUT_SECONDS));
  process.addParameter(F("-X"));
  process.addParameter(method);
  process.addParameter(F("-H"));
  process.addParameter(F("Content-Type: application/json"));
  if (String(API_TOKEN).length() > 0) {
    process.addParameter(F("-H"));
    process.addParameter(String(F("Authorization: Bearer ")) + API_TOKEN);
  }
  if (body.length() > 0) {
    process.addParameter(F("--data"));
    process.addParameter(body);
  }
  process.addParameter(String(API_BASE_URL) + path);

  const int exitCode = process.run();
  while (process.available()) response += static_cast<char>(process.read());
  return exitCode == 0;
}

bool loadProfiles() {
  String response;
  if (!requestApi(F("GET"), F(PROFILES_PATH), String(), response)) return false;

  JsonDocument document;
  if (deserializeJson(document, response)) return false;
  JsonArray profileArray = document.is<JsonArray>()
                               ? document.as<JsonArray>()
                               : document["profiles"].as<JsonArray>();
  profileCount = 0;
  for (JsonObjectConst item : profileArray) {
    if (profileCount >= PROFILE_LIMIT) break;
    const float minCm = item["min_cm"] | -1.0f;
    const float maxCm = item["max_cm"] | -1.0f;
    if (minCm <= 0.0f || maxCm <= minCm) continue;
    profiles[profileCount].id = item["id"].as<String>();
    profiles[profileCount].name = item["name"] | "Profile";
    profiles[profileCount].minCm = minCm;
    profiles[profileCount].maxCm = maxCm;
    ++profileCount;
  }
  selectedProfile = 0;
  return true;
}

void enterCounting(const Profile &profile) {
  activeProfile = profile;
  repCount = 0;
  repInProgress = false;
  appState = AppState::Counting;
  lastSensorAtMs = 0;
  lastDisplayAtMs = 0;
}

void startCalibration() {
  standingDistanceCm = -1.0f;
  leanDistanceCm = -1.0f;
  appState = AppState::CalibrateStanding;
  drawScreen(F("CALIBRATION 1/2"), F("Stand straight, arms"), F("against wall"), F("SELECT: MEASURE"));
}

bool saveNewProfile() {
  const float minCm = leanDistanceCm + PROFILE_THRESHOLD_MARGIN_CM;
  const float maxCm = standingDistanceCm - PROFILE_THRESHOLD_MARGIN_CM;
  if (standingDistanceCm <= leanDistanceCm || maxCm <= minCm) return false;

  activeProfile.id = String();
  activeProfile.name = F("Wall Push-up");
  activeProfile.minCm = minCm;
  activeProfile.maxCm = maxCm;

  if (offlineMode) return true;

  JsonDocument document;
  document["name"] = activeProfile.name;
  document["standing_cm"] = standingDistanceCm;
  document["lean_cm"] = leanDistanceCm;
  document["min_cm"] = minCm;
  document["max_cm"] = maxCm;
  String body;
  serializeJson(document, body);
  String response;
  if (!requestApi(F("POST"), F(PROFILES_PATH), body, response)) return false;

  JsonDocument result;
  if (!deserializeJson(result, response) && result["id"].is<String>()) {
    activeProfile.id = result["id"].as<String>();
  }
  return true;
}

void showCalibrationResult() {
  if (standingDistanceCm <= 0.0f || leanDistanceCm <= 0.0f ||
      standingDistanceCm <= leanDistanceCm + 2.0f * PROFILE_THRESHOLD_MARGIN_CM) {
    appState = AppState::CalibrateLean;
    drawScreen(F("CALIBRATION RANGE"), F("Stand and lean"), F("farther apart"), F("SELECT: RETRY LEAN"));
    return;
  }
  if (!saveNewProfile()) {
    appState = AppState::ApiError;
    retryProfileSave = true;
    drawScreen(F("SAVE FAILED"), F("SELECT: RETRY SAVE"), F("LEFT: USE OFFLINE"));
    return;
  }
  retryProfileSave = false;
  enterCounting(activeProfile);
}

void renderCounting(float distanceCm) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextWrap(false);
  display.setCursor(0, 0);
  String name = activeProfile.name;
  if (name.length() > 14) name = name.substring(0, 13) + ".";
  display.println(name);
  display.setTextSize(3);
  display.setCursor(0, 15);
  display.print(repCount);
  display.setTextSize(1);
  display.setCursor(76, 25);
  display.println(F("REPS"));
  display.setCursor(0, 46);
  display.println(distanceCm <= 0.0f ? F("WAIT") : (repInProgress ? F("NEAR") : F("STAND")));
  display.setCursor(64, 46);
  if (distanceCm > 0.0f) {
    display.print(distanceCm, 1);
    display.print(F(" cm"));
  } else {
    display.print(F("NO ECHO"));
  }
  display.setCursor(0, 56);
  display.println(F("SELECT: RESET"));
  display.display();
}

void connectOnline() {
  drawScreen(F("CONNECTING"), F("Starting Yun link"), F("Loading profiles..."));
  Bridge.begin();
  if (!loadProfiles()) {
    appState = AppState::ApiError;
    drawScreen(F("API UNAVAILABLE"), F("SELECT: RETRY"), F("LEFT: OFFLINE"));
    return;
  }
  appState = AppState::ProfileSelect;
  showProfileSelect();
}

void beginOfflineMode() {
  offlineMode = true;
  startCalibration();
}

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // The stated wiring holds each button LOW when idle and HIGH when pressed.
  pinMode(LEFT_PIN, INPUT);
  pinMode(SELECT_PIN, INPUT);
  pinMode(RIGHT_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS)) {
    Serial.println(F("OLED init failed"));
    for (;;) {}
  }

  startupAtMs = millis();
  showStartup(5);
}

void loop() {
  const unsigned long now = millis();
  const ButtonEvents left = updateButton(leftButton, now);
  const ButtonEvents select = updateButton(selectButton, now);
  const ButtonEvents right = updateButton(rightButton, now);

  if (appState == AppState::Startup) {
    if (left.pressed || select.pressed || right.pressed) {
      beginOfflineMode();
    } else if (now - startupAtMs >= STARTUP_WINDOW_MS) {
      connectOnline();
    } else {
      const uint8_t secondsRemaining = static_cast<uint8_t>((STARTUP_WINDOW_MS - (now - startupAtMs) + 999) / 1000);
      if (secondsRemaining != lastStartupSeconds) {
        lastStartupSeconds = secondsRemaining;
        showStartup(secondsRemaining);
      }
    }
    return;
  }

  if (appState == AppState::ProfileSelect) {
    if (select.longPress) {
      startCalibration();
    } else if (left.shortPress && profileCount > 0) {
      selectedProfile = (selectedProfile + profileCount - 1) % profileCount;
      showProfileSelect();
    } else if (right.shortPress && profileCount > 0) {
      selectedProfile = (selectedProfile + 1) % profileCount;
      showProfileSelect();
    } else if (select.shortPress && profileCount > 0) {
      enterCounting(profiles[selectedProfile]);
    }
    return;
  }

  if (appState == AppState::ApiError) {
    if (left.shortPress && retryProfileSave) {
      offlineMode = true;
      retryProfileSave = false;
      enterCounting(activeProfile);
    } else if (left.shortPress && !offlineMode) {
      beginOfflineMode();
    } else if (select.shortPress) {
      if (retryProfileSave) {
        showCalibrationResult();
      } else if (offlineMode) {
        startCalibration();
      } else {
        connectOnline();
      }
    }
    return;
  }

  if (appState == AppState::CalibrateStanding) {
    if (select.shortPress) {
      standingDistanceCm = captureDistanceCm();
      if (standingDistanceCm <= 0.0f) {
        drawScreen(F("NO ECHO"), F("Check sensor / stance"), F("SELECT: RETRY SAMPLE"));
      } else {
        appState = AppState::CalibrateLean;
        drawScreen(F("CALIBRATION 2/2"), F("Lean toward wall"), F("wall push-up position"), F("SELECT: MEASURE"));
      }
    }
    return;
  }

  if (appState == AppState::CalibrateLean) {
    if (select.shortPress) {
      leanDistanceCm = captureDistanceCm();
      showCalibrationResult();
    }
    return;
  }

  if (appState == AppState::Counting) {
    if (select.shortPress) {
      repCount = 0;
      repInProgress = false;
      renderCounting(-1.0f);
    }
    if (now - lastSensorAtMs >= SENSOR_INTERVAL_MS) {
      lastSensorAtMs = now;
      const float distanceCm = readDistanceCm();
      if (distanceCm > 0.0f) {
        if (distanceCm <= activeProfile.minCm) {
          repInProgress = true;
        } else if (distanceCm >= activeProfile.maxCm && repInProgress) {
          ++repCount;
          repInProgress = false;
        }
      }
      if (now - lastDisplayAtMs >= 150) {
        lastDisplayAtMs = now;
        renderCounting(distanceCm);
      }
    }
  }
}