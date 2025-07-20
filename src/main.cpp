#include <Arduino.h>
#include <Servo.h>

// ATMEL ATTINY84 / ARDUINO
//
//                           +-\/-+
//                     VCC  1|    |14  GND
//             (D  0)  PB0  2|    |13  PA0  (D 10)        AREF
//             (D  1)  PB1  3|    |12  PA1  (D  9)
//  RESET      (D 11)  PB3  4|    |11  PA2  (D  8)
//  PWM  INT0  (D  2)  PB2  5|    |10  PA3  (D  7)
//  PWM        (D  3)  PA7  6|    |9   PA4  (D  6)
//  PWM        (D  4)  PA6  7|    |8   PA5  (D  5)        PWM
//                           +----+

// Shift register pins

#define LATCH_PIN 10 // stcp
#define CLOCK_PIN 9 // shcp
#define DATA_PIN 8 // ds

// 7-segment digit pins

#define DIGIT_1_PIN 0
#define DIGIT_2_PIN 1
#define DIGIT_3_PIN 2

#define POT_PIN A3 // Potentiometer pin

#define BTN_PIN 5

#define SERVO_PIN 3 // Servo control pin

enum Mode {
    MODE_MANUAL,
    MODE_SWEEP,
    MODE_CENTER,
    MODE_CALIBRATE,
};

Mode currentMode = MODE_MANUAL;

const uint8_t SEG_A = 0b00000001;
const uint8_t SEG_B = 0b00000100;
const uint8_t SEG_C = 0b00010000;
const uint8_t SEG_D = 0b00100000;
const uint8_t SEG_E = 0b01000000;
const uint8_t SEG_F = 0b00000010;
const uint8_t SEG_G = 0b00001000;

const uint8_t digitMap[10] = {
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,             // '0'
  SEG_B | SEG_C,                                             // '1'
  SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                     // '2'
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                     // '3'
  SEG_B | SEG_C | SEG_F | SEG_G,                             // '4'
  SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                     // '5'
  SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,             // '6'
  SEG_A | SEG_B | SEG_C,                                     // '7'
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,     // '8'
  SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G              // '9'
};

Servo servo1;

// Button variables
bool lastButtonState = HIGH;
bool buttonState = HIGH;
bool awaitingDoublePress = false;
const unsigned long longPressDuration = 1000;
const unsigned long doublePressWindow = 300;
unsigned long lastDebounceTime = 0;
unsigned long lastPressTime = 0;
unsigned long debounceDelay = 50;
unsigned long buttonPressTime = 0;

uint16_t angle = 0; // Current angle for display
uint16_t potValue = 0; // Current potentiometer 

unsigned long sweepInterval = 15;

uint16_t servoMax = 2400; // Maximum servo pulse width
uint16_t servoMin = 600; // Minimum servo pulse width

uint16_t currentRange = 180;

bool isChoosingCalibrationSlot = false;

//TODO:
// + Button resets whole system I think

// Function prototypes
void displayDigit(char digit, uint8_t digitIndex = 0);
uint16_t readAveragedPotValue(uint8_t samples);
void handleModes();

void setup() {
    pinMode(DIGIT_1_PIN, OUTPUT);
    pinMode(DIGIT_2_PIN, OUTPUT);
    pinMode(DIGIT_3_PIN, OUTPUT);
    pinMode(LATCH_PIN, OUTPUT);
    pinMode(CLOCK_PIN, OUTPUT);
    pinMode(DATA_PIN, OUTPUT);
    pinMode(POT_PIN, INPUT);
    pinMode(BTN_PIN, INPUT_PULLUP); // LOW when pressed

    digitalWrite(DIGIT_1_PIN, HIGH);
    digitalWrite(DIGIT_2_PIN, LOW);
    digitalWrite(DIGIT_3_PIN, LOW);

    servo1.attach(SERVO_PIN);
    servo1.writeMicroseconds(1500); // Initialize servo to middle position

    currentMode = MODE_MANUAL; // Start in manual mode
}

void loop() {
    potValue = readAveragedPotValue(8); // Read potentiometer value

    handleModes();

    uint16_t h;
    uint16_t t;
    uint16_t o;

    if(currentMode == MODE_SWEEP) {
        h = sweepInterval / 100;
        t = (sweepInterval / 10) % 10;
        o = sweepInterval % 10;

        displayDigit('0' + o, 2);
        delay(4);
        displayDigit((sweepInterval >= 10) ? ('0' + t) : ' ', 1);
        delay(4);
        displayDigit((sweepInterval >= 100) ? ('0' + h) : ' ', 0);
        delay(4);
    }else {
        uint16_t angle = map(servo1.readMicroseconds(), servoMin, servoMax, 0, 180); // inverted the mapping to match the potentiometer
        uint16_t h = angle / 100;
        uint16_t t = (angle / 10) % 10;
        uint16_t o = angle % 10;

        displayDigit('0' + o, 2);
        delay(4);
        displayDigit((angle >= 10) ? ('0' + t) : ' ', 1);
        delay(4);
        displayDigit((angle >= 100) ? ('0' + h) : ' ', 0);
        delay(4);
    }

    if(digitalRead(BTN_PIN) != lastButtonState) {
        lastDebounceTime = millis();
    }

    if(millis() - lastDebounceTime > debounceDelay) {
        if(digitalRead(BTN_PIN) != buttonState) {
            buttonState = digitalRead(BTN_PIN);

            if(buttonState == LOW) {
                buttonPressTime = millis();
            }else {
                unsigned long pressDuration = millis() - buttonPressTime;

                if(pressDuration >= longPressDuration) {
                    currentMode = MODE_CENTER;
                    awaitingDoublePress = false;
                }else {
                    if(awaitingDoublePress && (millis() - lastPressTime <= doublePressWindow)) {
                        currentMode = (currentMode == MODE_MANUAL) ? MODE_SWEEP : MODE_MANUAL;
                        awaitingDoublePress = false;
                    }else {
                        awaitingDoublePress = true;
                        lastPressTime = millis();
                    }
                }
            }
        }
    }

    if(awaitingDoublePress && (millis() - lastPressTime > doublePressWindow)) {
        currentMode = MODE_CALIBRATE;
        isChoosingCalibrationSlot = true;
        awaitingDoublePress = false;
    }

    lastButtonState = digitalRead(BTN_PIN);
}

void handleModes() {
    static uint16_t lastPotValue = potValue;

    switch (currentMode)
    {
        case MODE_MANUAL: {
            uint16_t lastMicros = servo1.readMicroseconds();
            uint16_t targetMicros = map(potValue, 1023, 0, servoMin, servoMax);

            if(lastMicros < targetMicros) {
                lastMicros += min(10, targetMicros - lastMicros); // Increment towards target
            }else if(lastMicros > targetMicros) {
                lastMicros -= min(10, lastMicros - targetMicros); // Decrement towards target
            }

            servo1.writeMicroseconds(lastMicros);
            break;
        }

        case MODE_SWEEP: {
            static bool sweepingRight = true;
            static uint16_t sweepMicros = servoMin;
            static unsigned long lastSweepTime = 0;

            if(abs((int)potValue - (int)lastPotValue) > 5) {
                sweepInterval = map(potValue, 1023, 0, 5, 50); // Adjust sweep speed based on pot value
                lastPotValue = potValue;
            }

            if(millis() - lastSweepTime >= sweepInterval) {
                lastSweepTime = millis();

                if(sweepingRight) {
                    sweepMicros += 10; // Increment towards max
                    if(sweepMicros >= servoMax) {
                        sweepMicros = servoMax;
                        sweepingRight = false; // Change direction
                    }
                }else {
                    sweepMicros -= 10; // Decrement towards min
                    if(sweepMicros <= servoMin) {
                        sweepMicros = servoMin;
                        sweepingRight = true; // Change direction
                    }
                }

                servo1.writeMicroseconds(sweepMicros);
            }
            break;
        }

        case MODE_CALIBRATE: {
            
            break;
        }

        case MODE_CENTER: {
            servo1.writeMicroseconds((servoMax-servoMin) / 2); // Center the servo
            break;
        }
        
        default: {
            break;
        }
    }
}

void displayDigit(char digit, uint8_t digitIndex) {
    digitalWrite(DIGIT_1_PIN, LOW);
    digitalWrite(DIGIT_2_PIN, LOW);
    digitalWrite(DIGIT_3_PIN, LOW);

    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, 0xFF); // All segments off
    digitalWrite(LATCH_PIN, HIGH);

    if(digit >= '0' && digit <= '9') {
        uint8_t segments = digitMap[digit - '0'];
        digitalWrite(LATCH_PIN, LOW);
        shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, ~segments);
        digitalWrite(LATCH_PIN, HIGH);
    }else if (digit == ' ') {
        digitalWrite(LATCH_PIN, LOW);
        shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, 0xFF);
        digitalWrite(LATCH_PIN, HIGH);
    }

    switch (digitIndex) {
        case 0:
            digitalWrite(DIGIT_1_PIN, HIGH);
            break;
        case 1:
            digitalWrite(DIGIT_2_PIN, HIGH);
            break;
        case 2:
            digitalWrite(DIGIT_3_PIN, HIGH);
            break;
        default:
            return;
    }
}

uint16_t readAveragedPotValue(uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(POT_PIN);
  }
  return sum / samples;
}