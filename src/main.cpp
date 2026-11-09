const int trigPin = 9;
const int echoPin = 8;
void setup() {
 Serial.begin(115200);
 pinMode(trigPin, OUTPUT);
 pinMode(echoPin, INPUT);
}
//function declaration
double distance_calculation(unsigned long duration);

//take duration and and multiply it by 0.0343 divide by 2, and return distance
double distance_calculation(unsigned long duration){
  double distanceCm = duration;
 if (duration == 0) {
       distanceCm = -1.0;
     } else {
         distanceCm = duration * 0.0343 / 2.0;
     }
     return distanceCm;
}
//

void loop() {
 digitalWrite(trigPin, LOW);
 delayMicroseconds(2);
 digitalWrite(trigPin, HIGH);
 delayMicroseconds(10);
 digitalWrite(trigPin, LOW);
 unsigned long duration = pulseIn(echoPin, HIGH, 30000UL);
 double distanceCm = distance_calculation(duration);
 Serial.println(distanceCm);
 delay(100);
}