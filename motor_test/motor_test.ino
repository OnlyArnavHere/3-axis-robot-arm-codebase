/*
 * Individual motor test for the 3-axis robot arm (ESP32)
 *
 * Open Serial Monitor at 115200 baud, line ending "No line ending" or "Newline",
 * and send one character at a time:
 *
 *   1  shoulder servo sweep
 *   2  elbow servo sweep
 *   3  claw servo open/close
 *   4  base motor clockwise      (ramps through 3 speeds, 2 s each)
 *   5  base motor counter-clockwise
 *   6  base motor full speed 3 s (checks the driver/supply, ignores PWM)
 *   0  stop everything
 *
 * Nothing moves until you send a command. Pins match arm_controller.ino.
 */

#include <ESP32Servo.h>

const int PIN_SHOULDER = 26;
const int PIN_ELBOW    = 27;
const int PIN_CLAW     = 25;
const int PIN_IN1 = 32;
const int PIN_IN2 = 33;
const int PIN_ENA = 14;

Servo shoulder, elbow, claw;

void motorPwm(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_ENA, duty);
#else
  ledcWrite(8, duty);
#endif
}

void baseStop() {
  digitalWrite(PIN_IN1, LOW);
  digitalWrite(PIN_IN2, LOW);
  motorPwm(0);
}

void baseRun(int dir, int duty, unsigned long ms) {
  digitalWrite(PIN_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(PIN_IN2, dir > 0 ? LOW : HIGH);
  motorPwm(duty);
  Serial.printf("  base dir=%d duty=%d/255\n", dir, duty);
  delay(ms);
  baseStop();
  delay(300);
}

// Slow sweep so you can see/hear exactly where it stalls or jitters
void sweep(Servo& s, const char* name, int lo, int hi) {
  Serial.printf("%s: %d -> %d -> %d\n", name, (lo + hi) / 2, lo, hi);
  s.write((lo + hi) / 2); delay(600);
  for (int a = (lo + hi) / 2; a >= lo; a--) { s.write(a); delay(15); }
  delay(300);
  for (int a = lo; a <= hi; a++)            { s.write(a); delay(15); }
  delay(300);
  for (int a = hi; a >= (lo + hi) / 2; a--) { s.write(a); delay(15); }
  Serial.println("  done");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_ENA, 1000, 8);
#else
  ledcSetup(8, 1000, 8);
  ledcAttachPin(PIN_ENA, 8);
#endif
  baseStop();

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  shoulder.setPeriodHertz(50);
  elbow.setPeriodHertz(50);
  claw.setPeriodHertz(50);
  // Servos are attached only when tested, so idle servos don't twitch/draw current.

  Serial.println("\nMotor test ready. 1=shoulder 2=elbow 3=claw 4=base CW 5=base CCW 6=base full 0=stop");
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  switch (c) {
    case '1':
      shoulder.attach(PIN_SHOULDER, 500, 2400);
      sweep(shoulder, "Shoulder", 0, 180);
      shoulder.detach();
      break;
    case '2':
      elbow.attach(PIN_ELBOW, 500, 2400);
      sweep(elbow, "Elbow", 0, 90);
      elbow.detach();
      break;
    case '3':
      claw.attach(PIN_CLAW, 500, 2400);
      sweep(claw, "Claw", 0, 90);
      claw.detach();
      break;
    case '4':
    case '5': {
      int dir = (c == '4') ? 1 : -1;
      Serial.println(dir > 0 ? "Base: direction A" : "Base: direction B");
      baseRun(dir, 120, 2000);
      baseRun(dir, 180, 2000);
      baseRun(dir, 255, 2000);
      Serial.println("  done");
      break;
    }
    case '6':
      Serial.println("Base: full speed 3 s (ENA pin driven HIGH, no PWM)");
      motorPwm(0);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcDetach(PIN_ENA);
#else
      ledcDetachPin(PIN_ENA);
#endif
      pinMode(PIN_ENA, OUTPUT);
      digitalWrite(PIN_ENA, HIGH);
      digitalWrite(PIN_IN1, HIGH);
      digitalWrite(PIN_IN2, LOW);
      delay(3000);
      baseStop();
      digitalWrite(PIN_ENA, LOW);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      ledcAttach(PIN_ENA, 1000, 8);
#else
      ledcAttachPin(PIN_ENA, 8);
#endif
      Serial.println("  done");
      break;
    case '0':
      baseStop();
      shoulder.detach(); elbow.detach(); claw.detach();
      Serial.println("All stopped");
      break;
    default:
      break;   // ignore newline etc.
  }
}
