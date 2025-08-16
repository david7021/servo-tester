#include <Arduino.h>
#include <Servo.h>
#include <EEPROM.h>

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

const uint8_t digitMap[12] = {
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,             // '0'
    SEG_B | SEG_C,                                             // '1'
    SEG_A | SEG_B | SEG_D | SEG_E | SEG_G,                     // '2'
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_G,                     // '3'
    SEG_B | SEG_C | SEG_F | SEG_G,                             // '4'
    SEG_A | SEG_C | SEG_D | SEG_F | SEG_G,                     // '5'
    SEG_A | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,             // '6'
    SEG_A | SEG_B | SEG_C,                                     // '7'
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F | SEG_G,     // '8'
    SEG_A | SEG_B | SEG_C | SEG_D | SEG_F | SEG_G,             // '9'
    SEG_A | SEG_D | SEG_E | SEG_F,                             // 'C'
    SEG_G,                                                     // '-'
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

uint16_t potValue = 0; // Current potentiometer 

unsigned long sweepInterval = 15;

uint16_t servoMax[] = {2400, 2400, 2400}; // Maximum servo pulse width
uint16_t servoMin[] = {600, 600, 600}; // Minimum servo pulse width
bool awaitingServoMin = false;
bool awaitingServoMax = false;
uint8_t servoMinMaxSlot = 0; // 0 for slot1, 1 for slot2, 2 for slot3

uint16_t lastPotValue = -1;

// Function prototypes
void displaySaved();
void displayDigit(char digit, uint8_t digitIndex = 0);
uint16_t readAveragedPotValue(uint8_t samples);
void handleModes();
void outputBits(uint8_t bits);
void loadServoCalibration();
void saveServoCalibration();
void displayDigits(char digit1, char digit2 = ' ', char digit3 = ' ');
bool rotatedPot();
void displayNumber(uint8_t num);

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
    
    servo1.attach(SERVO_PIN, 400, 2600); // Attach servo with min/max pulse width
    loadServoCalibration(); // Load saved servo calibration values
}

void loop() {
    uint16_t rawPotValue = analogRead(POT_PIN);
    potValue = readAveragedPotValue(8); // Read potentiometer value

    handleModes();

    if(currentMode == MODE_CALIBRATE) {
        if(!(awaitingServoMin || awaitingServoMax)) {
            if(rawPotValue < 341) {
                displayDigits('C', '1', ' ');
            }else if(rawPotValue < 682) {
                displayDigits('C', '2', ' ');
            }else {
                displayDigits('C', '3', ' ');
            }
        }
    }else if(currentMode == MODE_SWEEP) {
        displayNumber(sweepInterval);
    }else {
        uint16_t angle = map(servo1.readMicroseconds(), servoMin[servoMinMaxSlot], servoMax[servoMinMaxSlot], 0, 180);

        displayNumber(angle);
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
                    if(currentMode == MODE_CALIBRATE) {
                        servoMinMaxSlot = (rawPotValue < 341) ? 0 : (rawPotValue > 682) ? 1 : 2;
                        displaySaved();
                        currentMode = MODE_MANUAL;
                    }else {
                        currentMode = MODE_CENTER;
                        awaitingDoublePress = false;
                    }
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
        if(currentMode == MODE_CALIBRATE) {
            if(awaitingServoMin) {
                servoMin[servoMinMaxSlot] = servo1.readMicroseconds();
                awaitingServoMin = false;
                awaitingServoMax = true;
                displaySaved();
            }else if(awaitingServoMax) {
                servoMax[servoMinMaxSlot] = servo1.readMicroseconds();
                awaitingServoMax = false;
                saveServoCalibration();
                displaySaved();
                currentMode = MODE_MANUAL; // Exit calibration mode after saving
            }else if(rawPotValue < 341 || rawPotValue > 682 || rawPotValue <= 1023) {
                awaitingServoMin = true;
                servoMinMaxSlot = (rawPotValue < 341) ? 0 : (rawPotValue > 682) ? 1 : 2;
                servo1.detach(); // Detach servo during calibration
                delay(50);
                servo1.attach(SERVO_PIN, 400, 2600); // Reattach with current min/max
            }
        }else currentMode = MODE_CALIBRATE;

        awaitingDoublePress = false;
    }

    lastButtonState = digitalRead(BTN_PIN);
    lastPotValue = potValue;
}


uint16_t sweepMicros = servoMin[servoMinMaxSlot];
bool sweepingRight = true;
unsigned long lastSweepTime = 0;
void handleModes() {
    switch (currentMode)
    {
        case MODE_MANUAL: {
            uint16_t lastMicros = servo1.readMicroseconds();
            uint16_t targetMicros = map(potValue, 0, 1023, servoMin[servoMinMaxSlot], servoMax[servoMinMaxSlot]);

            if(lastMicros < targetMicros) {
                lastMicros += min(10, targetMicros - lastMicros); // Increment towards target
            }else if(lastMicros > targetMicros) {
                lastMicros -= min(10, lastMicros - targetMicros); // Decrement towards target
            }

            servo1.writeMicroseconds(lastMicros);
            break;
        }

        case MODE_SWEEP: {
            if(rotatedPot()) {
                sweepInterval = map(potValue, 0, 1023, 5, 50); // Adjust sweep speed based on pot value
            }

            if(millis() - lastSweepTime >= sweepInterval) {
                lastSweepTime = millis();

                if(sweepingRight) {
                    sweepMicros += 10; // Increment towards max
                    if(sweepMicros >= servoMax[servoMinMaxSlot]) {
                        sweepMicros = servoMax[servoMinMaxSlot];
                        sweepingRight = false; // Change direction
                    }
                }else {
                    sweepMicros -= 10; // Decrement towards min
                    if(sweepMicros <= servoMin[servoMinMaxSlot]) {
                        sweepMicros = servoMin[servoMinMaxSlot];
                        sweepingRight = true; // Change direction
                    }
                }

                servo1.writeMicroseconds(sweepMicros);
            }
            break;
        }

        case MODE_CALIBRATE: {
            if(awaitingServoMin || awaitingServoMax) {
                uint16_t lastMicros = servo1.readMicroseconds();
                uint16_t targetMicros = map(potValue, 0, 1023, 400, 2600);

                if(lastMicros < targetMicros) {
                    lastMicros += min(5, targetMicros - lastMicros); // Increment towards target
                }else if(lastMicros > targetMicros) {
                    lastMicros -= min(5, lastMicros - targetMicros); // Decrement towards target
                }

                servo1.writeMicroseconds(lastMicros);
            }
            break;
        }

        case MODE_CENTER: {
            servo1.write(90); // Center the servo
            break;
        }
        
        default: {
            break;
        }
    }
}

void saveServoCalibration() {
    for (uint8_t i = 0; i < 3; i++) {
        EEPROM.put(i * sizeof(uint16_t), servoMin[i]);
        EEPROM.put(3 * sizeof(uint16_t) + i * sizeof(uint16_t), servoMax[i]);
    }
}

void loadServoCalibration() {
    for (uint8_t i = 0; i < 3; i++) {
        EEPROM.get(i * sizeof(uint16_t), servoMin[i]);
        EEPROM.get(3 * sizeof(uint16_t) + i * sizeof(uint16_t), servoMax[i]);
    }
}

void displaySaved() {
    // Show "---" for a short time to indicate save
    for(uint8_t i = 0; i < 50; i++) {
        displayDigits('-', '-', '-');
    }
    displayDigits(' ', ' ', ' ');
}

void displayNumber(uint8_t num) {
    char h = (num >= 100) ? ('0' + num / 100) : ' ';
    char t = (num >= 10)  ? ('0' + (num / 10) % 10) : ' ';
    char o = '0' + (num % 10);
    displayDigits(h, t, o);
}

bool rotatedPot() {
    if(abs((int)potValue - (int)lastPotValue) > 5) {
        return true;
    }
    return false;
}

void displayDigits(char digit1, char digit2, char digit3) {
    displayDigit(digit1, 0);
    delay(4);
    displayDigit(digit2, 1);
    delay(4);
    displayDigit(digit3, 2);
    delay(4);
}

void displayDigit(char digit, uint8_t digitIndex) {
    digitalWrite(DIGIT_1_PIN, LOW);
    digitalWrite(DIGIT_2_PIN, LOW);
    digitalWrite(DIGIT_3_PIN, LOW);

    outputBits(0xFF); // All segments off

    if(digit >= '0' && digit <= '9') {
        uint8_t segments = digitMap[digit - '0'];
        outputBits(~segments); // Invert segments for common cathode display
    }else if(digit == 'C') {
        outputBits(~digitMap[10]); // Display 'C'
    }else if(digit == '-') {
        outputBits(~digitMap[11]); // Display '-'
    }else if (digit == ' ') {
        outputBits(0xFF); // All segments off
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

void outputBits(uint8_t bits) {
    digitalWrite(LATCH_PIN, LOW);
    shiftOut(DATA_PIN, CLOCK_PIN, LSBFIRST, bits);
    digitalWrite(LATCH_PIN, HIGH);
}

uint16_t readAveragedPotValue(uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(POT_PIN);
  }
  return sum / samples;
}