#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Initialize SSD1306 OLED display connected to I2C (-1 indicates no reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const int trigPin = 9;
const int echoPin = 8;
const int buttonPin = 4; // Button on DIO 4 (Reserved for future use)

// Function declarations
double distance_calculation(unsigned long duration);
void display_distance(double distanceCm);

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  digitalWrite(trigPin, LOW); 

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed. Check wiring or address."));
    for(;;); 
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

// calculates speed of sound, returns distance in cm
double distance_calculation(unsigned long duration) {
  if (duration == 0) {
    return -1.0; // error or out of range
  } 
  return (duration * 0.0343) / 2.0; // /2 cuz round trip
}

void display_distance(double distanceCm) {
  Serial.println(distanceCm); 

  display.clearDisplay();

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Distance:"));

  // Render Measurement Data
  if (distanceCm < 0) {
    display.setTextSize(2);
    display.setCursor(0, 24);
    display.println(F("E: Out of Range"));

  } else {
    display.setTextSize(2);
    display.setCursor(0, 24);
    display.print(distanceCm); // Limits to 1 decimal place
    display.setTextSize(1);
    display.print(F(" cm"));
  }

  display.display(); // Push drawing buffer to the physical screen
}

void loop() {
  // generate pulse
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // measure round trip time
  unsigned long duration = pulseIn(echoPin, HIGH, 30000UL); 
  double distanceCm = distance_calculation(duration); 

  display_distance(distanceCm);
  delay(500);
}