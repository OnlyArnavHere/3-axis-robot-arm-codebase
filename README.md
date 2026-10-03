# 3-Axis Robot Arm (ESP32)

Firmware: `arm_controller/arm_controller.ino`

| Joint | Actuator | Range |
|---|---|---|
| Base rotation | DC motor + H-bridge driver | continuous (open-loop, timed 360°) |
| Shoulder (arm to base) | SG90 | 0–180° |
| Elbow (arm extension) | SG90 | 0–90° |
| Claw | SG90 | 0–90° (open/close) |

## Wiring (default pins, change at the top of the sketch)

| ESP32 | Connects to |
|---|---|
| GPIO 26 | Shoulder servo signal (orange) |
| GPIO 27 | Elbow servo signal |
| GPIO 25 | Claw servo signal |
| GPIO 32 | Driver IN1 |
| GPIO 33 | Driver IN2 |
| GPIO 14 | Driver ENA (PWM speed; remove the ENA jumper on an L298N) |
| GND | Driver GND, servo GND, external supply GND (**common ground is required**) |

**Power:** run the 3 servos from a separate 5 V supply (not the ESP32's 3.3 V pin;
a USB/4×AA pack rated ≥1 A is a good choice). Power the DC motor from the driver's
motor supply input. Add a 470–1000 µF capacitor across the servo 5 V rail.

## Use

1. Install the Arduino IDE, the **esp32** board package, and the **ESP32Servo** library.
2. Open the sketch, pick your ESP32 board, upload.
3. Join Wi-Fi `RobotArm` (password `robot1234`), open `http://192.168.4.1`.
4. Sliders move shoulder / elbow / claw. Hold the base buttons to rotate; the
   `360°` buttons run the motor for a fixed time.

## Calibrate

- The base has no position sensor, so set `BASE_TURN_MS` until the `360°`
  button makes exactly one turn (it will drift over time; add an encoder or
  limit switch if you need accuracy).
- If a joint moves backwards, set its `*_REVERSED` flag; if the base turns the
  wrong way, set `BASE_REVERSED`.
- Adjust `CLAW_OPEN` / `CLAW_CLOSED` and the min/max limits so the arm never
  forces against its own frame.

## Troubleshooting: test each motor on its own

Upload `motor_test/motor_test.ino`, open Serial Monitor at 115200 baud and send
one character: `1` shoulder, `2` elbow, `3` claw, `4`/`5` base each way, `6` base
at full speed without PWM, `0` stop.

| Symptom | Likely cause |
|---|---|
| Servo does nothing, others fine | Signal wire on the wrong pin, or no 5 V/GND to that servo |
| All servos jitter / ESP32 reboots when one moves | Servo supply too weak or shared with the ESP32; add a 470–1000 µF capacitor, use a separate ≥1 A 5 V supply, common GND |
| Servo buzzes at one end | Range is past the mechanical limit; narrow the min/max in the sketch |
| Base: `6` spins but `4`/`5` don't | ENA/PWM wiring (ENA jumper still fitted, or wrong pin) or `BASE_MIN_DUTY` too low |
| Base: `6` doesn't spin either | Motor supply to the driver, IN1/IN2 wiring, or motor/driver fault |
| Base spins only one way | One of IN1/IN2 wired wrong or driver channel damaged |
