/*
 * ATTiny412, 4MHz Internal
 *
 * Created: 4/9/2026
 *	Author: Timinator2020
 *  Charlieplexing functions modeled after the code found here:
 *    http://www.instructables.com/id/CharliePlexed-LED-string-for-the-Arduino/step7/Coding-the-Arduino/
 *
 */

/* ATTINY412 / ARDUINO
 *                      ____
 *               VCC  1|*   |8  GND
 *         (D0)  PA6  2|    |7  PA3  (D4)
 *         (D1)  PA7  3|    |6  PA0  (D5)  UPDI
 *    PWM  (D2)  PA1  4|____|5  PA2  (D3)  PWM
 */

#include <avr/sleep.h>

const int ButtonPin = PIN_PA2; // D3 - Pullup on the PCB (disable internal pullups)

volatile bool TriggerNow = true;
int RndNumber = 0;
int PastRnd = -1;
int available[8];
int Remaining = 0;

/********************** Charlieplexing setup **********************/
#define LED_COUNT 12 // Total number of LEDs on the PCB
int BlinkDelay = 120; // microseconds the LED will be on, then off. It can affect brightness, but mostly affects speed

// Arrays to hold pin masks
byte AnodeMask[LED_COUNT];
byte CathodeMask[LED_COUNT];

// LED pin connections => { Anode, Cathode }
//   These are NOT Arduino pin numbers (D1/D2/etc)! They are **PORTA bit positions**
//   These numbers correspond to the # after 'PA' in the pinout diagram above (for MegaTinyCore).
// PA6=6 (D0), PA7=7 (D1), PA1=1 (D2), PA2=2 (D3), PA3=3 (D4), PA0=0 (D5/UPDI)
const byte LEDConnections[LED_COUNT][2] = {  
  { 6 , 7 }, // LED1  (PA6/D0, PA7/D1)
  { 7 , 1 }, // LED3  (PA7/D1, PA1/D2)
  { 1 , 3 }, // LED5  (PA1/D2, PA3/D4)
  { 7 , 6 }, // LED2  (PA7/D1, PA6/D0)
  { 1 , 7 }, // LED4  (PA1/D2, PA7/D1)
  { 3 , 1 }, // LED6  (PA3/D4, PA1/D2)
  { 6 , 1 }, // LED7  (PA6/D0, PA1/D2)
  { 7 , 3 }, // LED9  (PA7/D1, PA3/D4)
  { 1 , 6 }, // LED8  (PA1/D2, PA6/D0)
  { 3 , 7 }, // LED10 (PA3/D4, PA7/D1)
  { 6 , 3 }, // LED11 (PA6/D0, PA3/D4)
  { 3 , 6 }  // LED12 (PA3/D4, PA6/D0)
};

/**************************************************************************/
/**                      LED Pattern Definitions                         **/
/**************************************************************************/
/*
 * LED location in bit array:
 * Bit: 0b [D12] [D11] [D10] [D8] [D9] [D7] [D6] [D4] [D2] [D5] [D3] [D1] 
 * 
 * Example LED patterns:
 * 
 * 0b000000000001, // LED 1
 * 0b000000000010, // LED 3
 * 0b000000000100, // LED 5
 * 0b000000001000, // LED 2
 * 0b000000010000, // LED 4
 * 0b000000100000, // LED 6
 * 0b000001000000, // LED 7
 * 0b000010000000, // LED 9
 * 0b000100000000, // LED 8
 * 0b001000000000, // LED 10
 * 0b010000000000, // LED 11
 * 0b100000000000  // LED 12
 * 
 * Turn on LEDs 3, 8 and 11:
 *   0b010100000010 // 2nd position is D11, 4th position is D8 & 11th position is D3
 */

const uint16_t DispAllOff[] PROGMEM = { // Turn everything off
  0b000000000000
};

const uint16_t DispAllOn[] PROGMEM = { // Turn everything on
  0b111111111111
};

const uint16_t DispRaceCW[] PROGMEM = { // 3 LEDs Race Clockwise
  0b000000000100, 0b001000000100, 0b011000000100, 0b111000000000, 
  0b110010000000, 0b100010100000, 0b000110100000, 0b000100110000, 
  0b000100010010, 0b000000010011, 0b000000001011, 0b000001001001, 
  0b000001001100, 0b001001000100, 0b011000000100, 0b111000000000, 
  0b110010000000, 0b100010100000, 0b000110100000, 0b000100110000, 
  0b000100010010, 0b000000010011, 0b000000001011, 0b000001001001, 
  0b000001001000, 0b000001000000
};

const uint16_t DispPlusSpin[] PROGMEM = { // "+" rotation
  0b100100000101, 0b001010011000, 0b010001100010, 0b100100000101,
  0b001010011000, 0b010001100010, 0b100100000101, 0b001010011000,
  0b010001100010, 0b100100000101, 0b001010011000, 0b010001100010
};

const uint16_t DispOscillate[] PROGMEM = { // 2 LEDs Racing back and forth
  0b000010001000, 0b100001100001, 0b010100000110, 0b001000010000,
  0b010100000110, 0b100001100001
};

const uint16_t DispKittyCorner[] PROGMEM = { // 2 LEDs race from top-left corner to bottom-right corner and back
  0b000000000100, 0b001001000000, 0b010000001000, 0b100000000001, 
  0b000010000010, 0b000000110000, 0b000100000000, 0b000000110000, 
  0b000010000010, 0b100000000001, 0b010000001000, 0b001001000000
};

const uint16_t DispRaceCCW[] PROGMEM = { // 3 LED Race Counter-Clockwise
  0b000001000000, 0b000001001000, 0b000001001001, 0b000000001011, 
  0b000000010011, 0b000100010010, 0b000100110000, 0b000110100000, 
  0b100010100000, 0b110010000000, 0b111000000000, 0b011000000100, 
  0b001001000100, 0b000001001100, 0b000001001001, 0b000000001011, 
  0b000000010011, 0b000100010010, 0b000100110000, 0b000110100000, 
  0b100010100000, 0b110010000000, 0b111000000000, 0b011000000100, 
  0b001000000100, 0b000000000100
};

const uint16_t DispCurtains[] PROGMEM = { // LEDs race from top-center to bottom-center in opposite directions
  0b010000000100, 0b100001000000, 0b000010001000, 0b000000100001,
  0b000100000010, 0b000000010000, 0b000100000010, 0b000000100001,
  0b000010001000, 0b100001000000, 0b010000000100, 0b001000000000
};


/**************************************************************************/
/**                     Charlieplexing functions                         **/
/**************************************************************************/

// Turns on the specified LED
void turnOn(int Led) {
  PORTA.DIRSET = (AnodeMask[Led] | CathodeMask[Led]); // Set both as outputs
  PORTA.OUTSET = AnodeMask[Led]; // Anode HIGH
  PORTA.OUTCLR = CathodeMask[Led]; // Cathode LOW
}

// Turns ALL LEDs off
void allOff() {
  // digitalWrite has to look up each pin mask, each time (not very efficient).
  // This does the same thing without lookups
  uint8_t Mask = (1 << 6) | (1 << 7) | (1 << 1) | (1 << 3);
  PORTA.DIRCLR = Mask;
  PORTA.OUTCLR = Mask;
}

// Loads a pattern from a specific PROGMEM array
template <size_t N>
void displayChar(const uint16_t (&Pattern)[N], int TranSpeed) {
  for (uint8_t Frame = 0; Frame < N; Frame++)
  {
    uint16_t DisplayData = pgm_read_word(&(Pattern[Frame]));

    for (int i = 0; i < TranSpeed; i++)
    {
      for (int j = 0; j < LED_COUNT; j++)
      {
        byte k = bitRead(DisplayData, j);
        if (k == 1)
        {
          turnOn(j);
          delayMicroseconds(BlinkDelay);
        }
        else
        {
          delayMicroseconds(BlinkDelay);
        }
        allOff();
      }
    }
  }
}

// Twinkle effect - Turns all LEDs on and randomly shuts LED(s) off for a short duration
void twinkleLEDs(int TranSpeed, int FramesOff, int SeqCount) {
  uint8_t lastIndex = 255;
  
  for (int frame = 0; frame < SeqCount; frame++) {
    // Select NEW random LED
    uint8_t offIdx;
    do {
      offIdx = random(0, LED_COUNT);
    } while (offIdx == lastIndex);
    lastIndex = offIdx;

    // Display total sequence frame duration (TranSpeed)
    for (int t = 0; t < TranSpeed; t++) {
      bool isOff = (t < FramesOff);

      for (uint8_t j = 0; j < LED_COUNT; j++) {
        // If in Off phase and current LED matches targeted index, turn off
        if (isOff && (j == offIdx)) {
          delayMicroseconds(BlinkDelay);
        } else {
          turnOn(j);
          delayMicroseconds(BlinkDelay);
          allOff();
        }
      }
    }
  }
}

// Generate a new random number (without repeating or using a previously used number)
int getRndNumber(int PCnt) { // PCnt=max # of patterns
  if (Remaining == 0) { // refill empty array
    for (int i = 0; i < PCnt; i++) {
      available[i] = i + 1; // {1, 2, 3, 4, 5, 6, 7, 8}
    }
    Remaining = PCnt;
  }

  int SelIdx = -1;
  int RandomNumber = -1;

  // Pick a random, non-sequential number. If 1 number remains and is sequential, use it anyway (prevents infinite loop)
  do {
    SelIdx = random(0, Remaining);
    RandomNumber = available[SelIdx];
  } while (Remaining > 1 && (RandomNumber == PastRnd + 1 || RandomNumber == PastRnd - 1 || RandomNumber == PastRnd));

  available[SelIdx] = available[Remaining - 1]; // Remove selected number from array
  Remaining--;

  PastRnd = RandomNumber;
  return RandomNumber;
}


/**************************************************************************/
/**                      Power Management functions                      **/
/**************************************************************************/

// Button (Execute) ISR
ISR(PORTA_PORT_vect) {
    //PORTA.INTFLAGS = 0xFF; // Clear all Port A flags
    PORTA.INTFLAGS = PORT_INT2_bm; // Clear PA2 interrupt flag
    TriggerNow = true;
}

// RTC ISR
ISR(RTC_CNT_vect) {
    RTC.INTFLAGS = RTC_OVF_bm; // Clear Overflow flag
    TriggerNow = true;
}

// Initialize all needed GPIO
void initGPIO(void) {
    // Disable input buffers on all pins to save power
    PORTA.PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN1CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN3CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN6CTRL = PORT_ISC_INPUT_DISABLE_gc;
    PORTA.PIN7CTRL = PORT_ISC_INPUT_DISABLE_gc;
    // Enable the input buffer for ButtonPin
    *(&PORTA.PIN0CTRL + digitalPinToBitPosition(ButtonPin)) = PORT_ISC_FALLING_gc;
    // If I set ButtonPin to the bit position (2) instead of PIN_PA2, I wouldn't need digitalPinToBitPosition(), but it would be more confusing
}

// Initialize the 32KHz internal ultra low power 32 kHz oscillator 
void initULPOsc(void) {
    // RTC Setup
    while (RTC.STATUS > 0);
    RTC.CLKSEL = RTC_CLKSEL_INT1K_gc; // 1024Hz ULP Osc
    // RTC.PER = Sleep time in seconds
    //RTC.PER = 3; // 3 seconds
    //RTC.PER = 10;  // 10 seconds
    //RTC.PER = 420;  // 7 Minutes
    RTC.PER = 900;  // 15 Minutes
    RTC.INTCTRL = RTC_OVF_bm; // Enable Overflow Interrupt
    
    // Start RTC with 1024 prescaler (RUNSTDBY is required!)
    RTC.CTRLA = RTC_PRESCALER_DIV1024_gc | RTC_RTCEN_bm | RTC_RUNSTDBY_bm;
}


/**************************************************************************/
/**                          Arduino Functions                           **/
/**************************************************************************/

void setup() {
  // Auto-generate LED masks
  for(int i = 0; i < LED_COUNT; i++) {
    AnodeMask[i] = (1 << LEDConnections[i][0]);
    CathodeMask[i] = (1 << LEDConnections[i][1]);
  }
  // initialize things
  initGPIO();
  initULPOsc();
  set_sleep_mode(SLEEP_MODE_STANDBY); // Must use SLEEP_MODE_STANDBY for RTC_CNT to function - otherwise it will not wake from PWR_DOWN
  sei(); // Enable Global Interrupts (for wakeup)
}


void loop() {
  if (TriggerNow) {
    // --- MAIN WAKE CODE HERE ---
      
    for (int demos=0; demos<3; demos++) {
      bool Random = true;
      if (Random) {
        RndNumber = getRndNumber(8);
      } else {
        RndNumber = (RndNumber % 8) + 1;
      }
      switch (RndNumber) {
        case 1: // Pattern 1 - All LEDs Flash
          for (int loop = 0; loop < 4; loop++) {
            displayChar(DispAllOff, 210); delay(50);
            displayChar(DispAllOn, 210); delay(150);
          }
          break;

        case 2: // Pattern 2 - 3 LEDs Race Clockwise
          for (int loop=0; loop<2; loop++) { displayChar(DispRaceCW, 70); }
          break;
          
        case 3: // Pattern 3 - '+' rotation
        	for (int loop=0; loop<3; loop++) { displayChar(DispPlusSpin, 70); }
          break;

        case 4: // Pattern 4 - 2 LEDs Racing back and forth
          for (int loop = 0; loop < 3; loop++) { displayChar(DispOscillate, 70); }
          break;

        case 5: // Pattern 5 - Top-left to bottom-right
          for (int loop = 0; loop < 3; loop++) { displayChar(DispKittyCorner, 70); }
          break;

        case 6: // Pattern 6 - Counter-Clockwise
          for (int loop = 0; loop < 2; loop++) { displayChar(DispRaceCCW, 70); }
          break;

        case 7: // Pattern 7 - Opposite directions
          for (int loop = 0; loop < 3; loop++) { displayChar(DispCurtains, 70); }
          break;

        case 8: // Twinkle pattern
          twinkleLEDs(70, 75, 40);
          break;
      }
    }

    // --- END WAKE CODE ---

    // Reset timer so we get a full cycle after a button press
    while (RTC.STATUS > 0); // Wait for RTC to be ready
    RTC.CNT = 0; // Reset counter
    while (RTC.STATUS > 0); // Wait for update to complete

    // reset for next wake cycle
    TriggerNow = false;
  }

  // Go to sleep
  if (!TriggerNow) { // Prevent sleep race condition
    // set_sleep_mode(SLEEP_MODE_STANDBY); // Set Sleep Mode (Must use SLEEP_MODE_STANDBY for RTC_CNT to function - otherwise it will not wake from PWR_DOWN)
    sleep_enable();
    sleep_cpu(); // Go to sleep

    // ... (: SLEEPING :) ...

    // Wake back up
    // wakeUp();
    sleep_disable();
    initGPIO(); // initialize GPIO
  }
}