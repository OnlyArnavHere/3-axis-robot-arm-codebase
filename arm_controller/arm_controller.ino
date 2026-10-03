/*
 * 3-axis robot arm + claw  -  ESP32
 *
 *   Base rotation : DC motor via H-bridge driver (L298N / L293D / TB6612 style)
 *   Shoulder      : SG90 servo, 0-180 deg
 *   Elbow         : SG90 servo, 0-90 deg
 *   Claw          : SG90 servo, 0-90 deg (open/close)
 *
 * Control: the ESP32 creates its own Wi-Fi network and serves a control page.
 *   1. Connect your phone/laptop to Wi-Fi  "RobotArm"  (password in WIFI_PASS)
 *   2. Open  http://192.168.4.1  in a browser
 *
 * Required library (Arduino Library Manager): "ESP32Servo" by Kevin Harrington
 * Board package: "esp32" by Espressif (works with core 2.x and 3.x)
 *
 * NOTE ON THE BASE: a plain DC motor has no position feedback, so "360 deg" is
 * done open-loop by running the motor for a fixed time. Calibrate
 * BASE_TURN_MS below until one press of the 360 button = one full rotation.
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ------------------------------- CONFIG ------------------------------------
const char* WIFI_SSID = "RobotArm";
const char* WIFI_PASS = "robot1234";          // min 8 characters

// Servo pins (signal wires)
const int PIN_SHOULDER = 26;
const int PIN_ELBOW    = 27;
const int PIN_CLAW     = 25;

// DC motor driver pins (L298N: IN1, IN2, ENA)
const int PIN_IN1 = 32;
const int PIN_IN2 = 33;
const int PIN_ENA = 14;                       // PWM speed. Remove ENA jumper on L298N.

// Servo travel limits (degrees)
const int SHOULDER_MIN = 0,  SHOULDER_MAX = 180, SHOULDER_HOME = 90;
const int ELBOW_MIN    = 0,  ELBOW_MAX    = 90,  ELBOW_HOME    = 45;
const int CLAW_MIN     = 0,  CLAW_MAX     = 90;               // 0 = closed, 90 = open
const int CLAW_OPEN    = 90, CLAW_CLOSED  = 0;
const int CLAW_HOME    = CLAW_OPEN;

// If a servo moves the wrong way, flip it here
const bool SHOULDER_REVERSED = false;
const bool ELBOW_REVERSED    = false;
const bool CLAW_REVERSED     = false;

// Servo smoothing: degrees per step, one step every SERVO_STEP_MS
const int SERVO_STEP_DEG = 1;
const unsigned long SERVO_STEP_MS = 12;

// Base motor
const int  BASE_MIN_DUTY      = 90;           // below this the motor just hums (0-255)
const bool BASE_REVERSED      = false;        // flip if "right" turns the wrong way
const unsigned long BASE_TURN_MS = 3000;      // CALIBRATE: time for one full 360 at BASE_TURN_DUTY
const int  BASE_TURN_DUTY     = 200;          // speed used for the timed 360 (0-255)
const unsigned long BASE_HOLD_TIMEOUT_MS = 400; // stop if no "keep moving" message (safety)
// ---------------------------------------------------------------------------

WebServer server(80);
Servo shoulder, elbow, claw;

int shoulderCur = SHOULDER_HOME, shoulderTgt = SHOULDER_HOME;
int elbowCur    = ELBOW_HOME,    elbowTgt    = ELBOW_HOME;
int clawCur     = CLAW_HOME,     clawTgt     = CLAW_HOME;
unsigned long lastServoStep = 0;

// Base state
int  baseDir = 0;                 // -1, 0, +1
int  baseDuty = 0;
bool baseTimed = false;
unsigned long baseTimedEnd = 0;
unsigned long baseLastCmd = 0;

// ----------------------------- Web page ------------------------------------
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Robot Arm</title>
<style>
 body{font-family:system-ui,sans-serif;background:#111;color:#eee;margin:0;padding:16px;max-width:480px;margin:auto}
 h2{margin:.2em 0 .6em} h3{margin:1.2em 0 .3em;font-weight:500;color:#9cf}
 input[type=range]{width:100%;height:36px}
 .row{display:flex;gap:8px;margin:8px 0}
 button{flex:1;padding:18px 6px;font-size:16px;border:0;border-radius:10px;background:#2b6cff;color:#fff;touch-action:none;user-select:none}
 button.alt{background:#444} button.stop{background:#d33}
 .val{float:right;color:#aaa}
</style></head><body>
<h2>Robot Arm</h2>

<h3>Base (DC motor) <span class="val" id="sp"></span></h3>
<input type="range" id="speed" min="10" max="100" value="60">
<div class="row">
 <button id="bl">&#8634; Hold</button>
 <button id="br">Hold &#8635;</button>
</div>
<div class="row">
 <button class="alt" onclick="turn(-1)">360&deg; &#8634;</button>
 <button class="stop" onclick="stopBase()">STOP</button>
 <button class="alt" onclick="turn(1)">360&deg; &#8635;</button>
</div>

<h3>Shoulder (0-180) <span class="val" id="vs"></span></h3>
<input type="range" id="s" min="0" max="180" value="90">
<h3>Elbow (0-90) <span class="val" id="ve"></span></h3>
<input type="range" id="e" min="0" max="90" value="45">
<h3>Claw (0-90) <span class="val" id="vc"></span></h3>
<input type="range" id="c" min="0" max="90" value="90">
<div class="row">
 <button class="alt" onclick="claw(90)">Open</button>
 <button class="alt" onclick="claw(0)">Close</button>
</div>

<script>
const $=id=>document.getElementById(id);
const send=u=>fetch(u).catch(()=>{});
function bindSlider(id,lbl){
  const el=$(id),v=$(lbl);let t=0;
  const upd=()=>{v.textContent=el.value+'°'};upd();
  el.oninput=()=>{upd();const n=Date.now();if(n-t>40){t=n;send('/set?'+id+'='+el.value)}};
  el.onchange=()=>send('/set?'+id+'='+el.value);
}
bindSlider('s','vs');bindSlider('e','ve');bindSlider('c','vc');
function claw(a){$('c').value=a;$('vc').textContent=a+'°';send('/set?c='+a)}
$('speed').oninput=()=>{$('sp').textContent=$('speed').value+'%'};$('sp').textContent='60%';
let hold=null;
function startHold(d){stopHold();const go=()=>send('/base?dir='+d+'&speed='+$('speed').value);go();hold=setInterval(go,150)}
function stopHold(){if(hold){clearInterval(hold);hold=null;send('/base?dir=0')}}
function stopBase(){stopHold();send('/base?dir=0')}
function turn(d){send('/turn?dir='+d)}
[['bl',-1],['br',1]].forEach(([id,d])=>{
  const b=$(id);
  b.addEventListener('pointerdown',e=>{e.preventDefault();startHold(d)});
  ['pointerup','pointerleave','pointercancel'].forEach(ev=>b.addEventListener(ev,stopHold));
});
</script></body></html>
)HTML";

// ----------------------------- Base motor ----------------------------------
void motorPwm(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_ENA, duty);
#else
  ledcWrite(8, duty);   // channel 8 = low-speed group, separate from servo timers
#endif
}

void baseDrive(int dir, int duty) {
  baseDir = dir;
  baseDuty = duty;
  if (BASE_REVERSED) dir = -dir;
  if (dir == 0) {
    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    motorPwm(0);
    return;
  }
  digitalWrite(PIN_IN1, dir > 0 ? HIGH : LOW);
  digitalWrite(PIN_IN2, dir > 0 ? LOW : HIGH);
  motorPwm(constrain(duty, 0, 255));
}

void baseStop() {
  baseTimed = false;
  baseDrive(0, 0);
}

// ----------------------------- Servos --------------------------------------
int servoAngle(int deg, int lo, int hi, bool rev) {
  deg = constrain(deg, lo, hi);
  return rev ? (lo + hi - deg) : deg;
}

void writeServos() {
  shoulder.write(servoAngle(shoulderCur, SHOULDER_MIN, SHOULDER_MAX, SHOULDER_REVERSED));
  elbow.write(servoAngle(elbowCur, ELBOW_MIN, ELBOW_MAX, ELBOW_REVERSED));
  claw.write(servoAngle(clawCur, CLAW_MIN, CLAW_MAX, CLAW_REVERSED));
}

int stepToward(int cur, int tgt) {
  if (cur < tgt) return min(cur + SERVO_STEP_DEG, tgt);
  if (cur > tgt) return max(cur - SERVO_STEP_DEG, tgt);
  return cur;
}

void updateServos() {
  unsigned long now = millis();
  if (now - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = now;
  int s = stepToward(shoulderCur, shoulderTgt);
  int e = stepToward(elbowCur, elbowTgt);
  int c = stepToward(clawCur, clawTgt);
  if (s != shoulderCur || e != elbowCur || c != clawCur) {
    shoulderCur = s; elbowCur = e; clawCur = c;
    writeServos();
  }
}

// ----------------------------- HTTP handlers -------------------------------
void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleSet() {
  if (server.hasArg("s")) shoulderTgt = constrain(server.arg("s").toInt(), SHOULDER_MIN, SHOULDER_MAX);
  if (server.hasArg("e")) elbowTgt    = constrain(server.arg("e").toInt(), ELBOW_MIN, ELBOW_MAX);
  if (server.hasArg("c")) clawTgt     = constrain(server.arg("c").toInt(), CLAW_MIN, CLAW_MAX);
  server.send(200, "text/plain", "ok");
}

void handleBase() {
  int dir = constrain(server.arg("dir").toInt(), -1, 1);
  int speed = constrain(server.arg("speed").toInt(), 0, 100);
  baseTimed = false;
  if (dir == 0) {
    baseStop();
  } else {
    int duty = map(speed, 0, 100, BASE_MIN_DUTY, 255);
    baseLastCmd = millis();
    baseDrive(dir, duty);
  }
  server.send(200, "text/plain", "ok");
}

void handleTurn() {
  int dir = server.arg("dir").toInt() >= 0 ? 1 : -1;
  baseTimed = true;
  baseTimedEnd = millis() + BASE_TURN_MS;
  baseDrive(dir, BASE_TURN_DUTY);
  server.send(200, "text/plain", "ok");
}

// ----------------------------- Setup / loop --------------------------------
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
  shoulder.attach(PIN_SHOULDER, 500, 2400);
  elbow.attach(PIN_ELBOW, 500, 2400);
  claw.attach(PIN_CLAW, 500, 2400);
  writeServos();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_SSID, WIFI_PASS);
  Serial.print("Connect to Wi-Fi '"); Serial.print(WIFI_SSID);
  Serial.print("' and open http://"); Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/base", handleBase);
  server.on("/turn", handleTurn);
  server.begin();
}

void loop() {
  server.handleClient();
  updateServos();

  unsigned long now = millis();
  if (baseTimed) {
    if ((long)(now - baseTimedEnd) >= 0) baseStop();
  } else if (baseDir != 0 && now - baseLastCmd > BASE_HOLD_TIMEOUT_MS) {
    baseStop();   // client stopped talking (finger lifted / Wi-Fi lost)
  }
}
