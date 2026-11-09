const int trig_pin = 9;
const int echo_pin = 10;

bool state = false;

const float near_threshold = 33.0;
const float return_threshold = 38.0;

int rep_count = 0;

float minimum = 0;
float maximum = 0;

unsigned long start_time = 0;
unsigned long previous_time = 0;

float previous_distance = 0;
bool has_previous = false;

void setup() {
  Serial.begin(115200);
  pinMode(trig_pin, OUTPUT);
  pinMode(echo_pin, INPUT);
}

void loop() {
  digitalWrite(trig_pin, LOW);
  delayMicroseconds(2);
  digitalWrite(trig_pin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig_pin, LOW);

  unsigned long duration = pulseIn(echo_pin, HIGH, 30000UL);

  if (duration == 0) {
    has_previous = false;
    delay(100);
    return;
  }

  float distance_cm = duration * 0.0343 / 2.0;
  unsigned long current_time = millis();

  // Calculate velocity and speed
  if (has_previous && current_time > previous_time) {
    float velocity = (distance_cm - previous_distance)
                     * 1000.0
                     / (current_time - previous_time);

    float speed = abs(velocity);

    Serial.print("Speed (cm/s): ");
    Serial.println(speed);
  }

  previous_distance = distance_cm;
  previous_time = current_time;
  has_previous = true;

  // Start a repetition
  if (!state && distance_cm <= near_threshold) {
    state = true;
    start_time = current_time;

    minimum = distance_cm;
    maximum = distance_cm;
  }

  // Track an ongoing repetition
  else if (state) {
    minimum = min(minimum, distance_cm);
    maximum = max(maximum, distance_cm);

    // Complete the repetition
    if (distance_cm >= return_threshold) {
      rep_count++;

      float movement_range = maximum - minimum;
      float rep_time = (current_time - start_time) / 1000.0;

      Serial.print("Repetition: ");
      Serial.println(rep_count);

      Serial.print("Movement range (cm): ");
      Serial.println(movement_range);

      Serial.print("Repetition time (s): ");
      Serial.println(rep_time);

      state = false;
    }
  }

  delay(100);
}
