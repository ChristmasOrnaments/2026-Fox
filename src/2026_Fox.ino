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

#define DISPLAY_SEQUENCE(seq, speed) displaySequence(seq, sizeof(seq) / sizeof(seq[0]), speed)

constexpr uint8_t ButtonPin = PIN_PA2; // D3 - Pullup on the PCB (disable internal pullups)
constexpr uint8_t LED_COUNT = 12; // Total number of LEDs on the PCB
constexpr uint8_t BlinkDelay = 120; // microseconds the LED will be on, then off. It can affect brightness, but mostly affects speed
constexpr uint8_t SequenceCnt = 8; // Total number of sequences to display

volatile bool TriggerNow = true;

// Arrays to hold pin masks
byte AnodeMask[LED_COUNT];
byte CathodeMask[LED_COUNT];

// LED pin connections => { Anode, Cathode }
//   These are NOT Arduino pin numbers (D1/D2/etc)! They are **PORTA bit positions**
//   These numbers correspond to the # after 'PA' in the pinout diagram above (for MegaTinyCore).
// PA6=6 (D0), PA7=7 (D1), PA1=1 (D2), PA2=2 (D3), PA3=3 (D4), PA0=0 (D5/UPDI)
constexpr uint8_t LEDConnections[LED_COUNT][2] = {  
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
/**                      LED Sequence Definitions                        **/
/**************************************************************************/
/*
 * LED location in bit array:
 * Bit: 0b [D12] [D11] [D10] [D8] [D9] [D7] [D6] [D4] [D2] [D5] [D3] [D1] 
 * 
 * Example LED sequences:
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

// NOTE:
// * On modern megaAVR architectures like the ATtiny412 (AVR 0-series), Flash memory is mapped into the unified 16-bit data address space.
// * Flash data can be read directly via regular pointer dereferencing without requiring macros like pgm_read_word().

constexpr uint16_t SeqAllOff[] = { // Turn everything off
  0b000000000000
};

constexpr uint16_t SeqAllOn[] = { // Turn everything on
  0b111111111111
};

constexpr uint16_t SeqRaceCW[] = { // 3 LEDs Race Clockwise
  0b000000000100, 0b001000000100, 0b011000000100, 0b111000000000, 
  0b110010000000, 0b100010100000, 0b000110100000, 0b000100110000, 
  0b000100010010, 0b000000010011, 0b000000001011, 0b000001001001, 
  0b000001001100, 0b001001000100, 0b011000000100, 0b111000000000, 
  0b110010000000, 0b100010100000, 0b000110100000, 0b000100110000, 
  0b000100010010, 0b000000010011, 0b000000001011, 0b000001001001, 
  0b000001001000, 0b000001000000
};

constexpr uint16_t SeqPlusSpin[] = { // "+" rotation
  0b100100000101, 0b001010011000, 0b010001100010, 0b100100000101,
  0b001010011000, 0b010001100010, 0b100100000101, 0b001010011000,
  0b010001100010, 0b100100000101, 0b001010011000, 0b010001100010
};

constexpr uint16_t SeqOscillate[] = { // 2 LEDs Racing back and forth
  0b000010001000, 0b100001100001, 0b010100000110, 0b001000010000,
  0b010100000110, 0b100001100001
};

constexpr uint16_t SeqKittyCorner[] = { // 2 LEDs race from top-left corner to bottom-right corner and back
  0b000000000100, 0b001001000000, 0b010000001000, 0b100000000001, 
  0b000010000010, 0b000000110000, 0b000100000000, 0b000000110000, 
  0b000010000010, 0b100000000001, 0b010000001000, 0b001001000000
};

constexpr uint16_t SeqRaceCCW[] = { // 3 LED Race Counter-Clockwise
  0b000001000000, 0b000001001000, 0b000001001001, 0b000000001011, 
  0b000000010011, 0b000100010010, 0b000100110000, 0b000110100000, 
  0b100010100000, 0b110010000000, 0b111000000000, 0b011000000100, 
  0b001001000100, 0b000001001100, 0b000001001001, 0b000000001011, 
  0b000000010011, 0b000100010010, 0b000100110000, 0b000110100000, 
  0b100010100000, 0b110010000000, 0b111000000000, 0b011000000100, 
  0b001000000100, 0b000000000100
};

constexpr uint16_t SeqCurtains[] = { // LEDs race from top-center to bottom-center in opposite directions
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

// Loads a sequence from array reference
void displaySequence(const uint16_t* sequence, size_t count, int tranSpeed) {
  for (size_t frame = 0; frame < count; frame++) {
    uint16_t displayData = sequence[frame];

    for (int i = 0; i < tranSpeed; i++) {
      for (int j = 0; j < LED_COUNT; j++) {
        if (bitRead(displayData, j)) {
          turnOn(j);
          delayMicroseconds(BlinkDelay);
        } else {
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

// Get the next random number (Fisher-Yates Shuffle)
int getRndNumber() {
  static int PastRnd = -1;
  static int RndAvailable[8];
  static int RndIdx = SequenceCnt;

  if (RndIdx >= SequenceCnt) { // Reshuffle if exhausted
    for (int i = 0; i < SequenceCnt; i++) { 
      RndAvailable[i] = i + 1;
    }
    
    for (int i = SequenceCnt - 1; i > 0; i--) { // Fisher-Yates Shuffle
      int idx = random(0, i + 1);
      int k = RndAvailable[i];
      RndAvailable[i] = RndAvailable[idx];
      RndAvailable[idx] = k;
    }
    
    // Ensure the first element of the new cycle isn't consecutive to the last element of the previous cycle
    if (abs(RndAvailable[0] - PastRnd) <= 1 && SequenceCnt > 1) {
      int k = RndAvailable[0];
      RndAvailable[0] = RndAvailable[1];
      RndAvailable[1] = k;
    }
    
    RndIdx = 0;
  }

  PastRnd = RndAvailable[RndIdx];
  return RndAvailable[RndIdx++];
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
  static int SequenceIdx = 0;
  
  if (TriggerNow) {
    // --- MAIN WAKE CODE HERE ---
      
    for (int demos=0; demos<3; demos++) {
      bool Random = true;
      if (Random) {
        SequenceIdx = getRndNumber();
      } else {
        SequenceIdx = (SequenceIdx % SequenceCnt) + 1;
      }
      switch (SequenceIdx) {
        case 1: // Sequence 1 - All LEDs Flash
          for (int loop = 0; loop < 4; loop++) {
            DISPLAY_SEQUENCE(SeqAllOff, 210); delay(50);
            DISPLAY_SEQUENCE(SeqAllOn, 210); delay(150);
          }
          break;

        case 2: // Sequence 2 - 3 LEDs Race Clockwise
          for (int loop=0; loop<2; loop++) { DISPLAY_SEQUENCE(SeqRaceCW, 70); }
          break;
          
        case 3: // Sequence 3 - '+' rotation
        	for (int loop=0; loop<3; loop++) { DISPLAY_SEQUENCE(SeqPlusSpin, 70); }
          break;

        case 4: // Sequence 4 - 2 LEDs Racing back and forth
          for (int loop = 0; loop < 4; loop++) { DISPLAY_SEQUENCE(SeqOscillate, 70); }
          break;

        case 5: // Sequence 5 - Top-left to bottom-right
          for (int loop = 0; loop < 3; loop++) { DISPLAY_SEQUENCE(SeqKittyCorner, 70); }
          break;

        case 6: // Sequence 6 - Counter-Clockwise
          for (int loop = 0; loop < 2; loop++) { DISPLAY_SEQUENCE(SeqRaceCCW, 70); }
          break;

        case 7: // Sequence 7 - Opposite directions
          for (int loop = 0; loop < 3; loop++) { DISPLAY_SEQUENCE(SeqCurtains, 70); }
          break;

        case 8: // Twinkle sequence
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