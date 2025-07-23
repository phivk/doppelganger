/*
 * ==============================================================================
 * DOPPELGÄNGER - Declarative LED Animation Framework
 * ==============================================================================
 * 
 * This Arduino sketch implements a declarative animation framework for controlling
 * multiple LED strips divided into logical "parts" that can be animated using
 * command-based compositions with 1-indexed bitmask representation.
 * 
 * HARDWARE SETUP:
 * - Strip 1: Connected to pin 2, 60 LEDs (GRBW NeoPixels)
 * - Strip 2: Connected to pin 3, 60 LEDs (GRBW NeoPixels)
 * - Strip 3: Connected to pin 4, 60 LEDs (GRBW NeoPixels)
 * - Strip 4: Connected to pin 5, 60 LEDs (GRBW NeoPixels)
 * 
 * PART LAYOUT (1-INDEXED):
 * The system uses 4 logical parts arranged in a frame:
 * - Part 1: Strip1, LEDs 36-59 (Front A-Side) - Bitmask: 0b0001
 * - Part 2: Strip2, LEDs 36-59 (Front B-Side) - Bitmask: 0b0010
 * - Part 3: Strip3, LEDs 36-59 (Back A-Side)  - Bitmask: 0b0100
 * - Part 4: Strip4, LEDs 36-59 (Back B-Side)  - Bitmask: 0b1000
 * 
 * Physical Layout:
 *   Front: [Part 1] [Part 2]
 *   Back:  [Part 3] [Part 4]
 *          A Side   B Side
 * 
 * CRITICAL CONSTRAINT:
 * - Only FRONT or BACK can be lit at any time, never both
 * - Front parts (1 & 2) can be on together - FRONT_MASK: 0b0011
 * - Back parts (3 & 4) can be on together  - BACK_MASK:  0b1100
 * - Diagonal parts (1 & 4) or (2 & 3) can be on together - DIAGONAL_MASKS
 * - Side combinations (1 & 3) or (2 & 4) are forbidden
 * 
 * DECLARATIVE SYSTEM:
 * Animations are defined using command arrays with three command types:
 * - ANIMATE: Start animation on parts specified by bitmask
 * - WAIT: Pause for specified duration
 * - WAIT_COMPLETE: Wait for all active animations to finish
 * 
 * TEMPO SYSTEM:
 * 20-second cycles with dynamic tempo: slow (0.3x) → fast (3.0x) → slow
 * All durations automatically adjust to current tempo for evolving experience
 * 
 * ==============================================================================
 */

#include <Adafruit_NeoPixel.h>
#include <math.h>

// === HARDWARE CONFIGURATION ===
#define PIN1 2              // Strip 1 data pin
#define PIN2 3              // Strip 2 data pin
#define PIN3 4              // Strip 3 data pin
#define PIN4 5              // Strip 4 data pin
#define NUM_LEDS 60         // LEDs per strip
#define BRIGHTNESS 50       // Global brightness (0-255)

// Button and LED configuration
#define BUTTON_PIN 7        // Button connected to pin 7
#define CONTROL_LED_PIN 8   // Control LED connected to pin 8

Adafruit_NeoPixel strip1(NUM_LEDS, PIN1, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, PIN2, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel strip3(NUM_LEDS, PIN3, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel strip4(NUM_LEDS, PIN4, NEO_GRBW + NEO_KHZ800);

// === PART-BASED ANIMATION FRAMEWORK ===

// Animation types define different lighting behaviors
enum AnimationType {
  OFF,                // Immediate off
  FADE_IN,           // 0 → full brightness over duration
  FADE_OUT,          // full brightness → 0 over duration  
  PULSE,             // fade in then fade out (complete cycle)
  BREATHE,           // slow inhale (70%), quick exhale (30%)
  WIPE_IN_FROM_TOP,  // wipe in from top to bottom
  WIPE_IN_FROM_BOTTOM,   // wipe in from bottom to top
  WIPE_OUT_FROM_TOP,     // wipe out from top to bottom
  WIPE_OUT_FROM_BOTTOM,   // wipe out from bottom to top
  SHIMMER,           // rapid micro-pulses at different intensities
  TWINKLE,           // individual LEDs randomly fade in/out like stars
  SPIRAL,            // wipe with varying speeds creating spiral effect
  FLUTTER,           // quick organic brightness variations like candlelight
  CASCADE,           // multi-stage wave where intensity builds then releases
  STROBE             // precise rhythmic flashing with customizable patterns
};

// Animation state tracks the progress of each part's current animation
struct AnimationState {
  AnimationType type;         // What animation is running
  uint32_t startTime;        // When animation started (millis)
  uint16_t duration;         // How long animation should take
  bool isActive;             // Whether animation is currently running
};

// LED Part represents a contiguous section of LEDs that can be animated as a unit
struct LEDPart {
  Adafruit_NeoPixel* strip;     // Which physical strip this part belongs to
  int startLED;                 // First LED index in this part
  int endLED;                   // Last LED index in this part
  uint8_t currentBrightness;    // Current brightness level (0-255)
  AnimationState animation;     // Current animation state
};

// Define the 4 parts that make up our LED layout
// NOTE: Array is 0-indexed for internal use, but bitmasks are 1-indexed
// Part 1 (bitmask 0b0001) maps to parts[0], Part 2 (0b0010) to parts[1], etc.
LEDPart parts[] = {
  {&strip1, 36, 59, 0, {OFF, 0, 0, false}},   // parts[0] = Part 1: Strip1 LEDs 36-59
  {&strip2, 36, 59, 0, {OFF, 0, 0, false}},   // parts[1] = Part 2: Strip2 LEDs 36-59
  {&strip3, 36, 59, 0, {OFF, 0, 0, false}},   // parts[2] = Part 3: Strip3 LEDs 36-59
  {&strip4, 36, 59, 0, {OFF, 0, 0, false}}    // parts[3] = Part 4: Strip4 LEDs 36-59
};

#define NUM_PARTS 4

// Button state variables
int buttonState = 0;
int lastButtonState = 0;
bool buttonPressed = false;

// System state variables
enum SystemState {
  IDLE,
  PLAYING_COMPOSITION
};

SystemState currentState = IDLE;
uint32_t idleStartTime = 0;
int currentCompositionIndex = 0;


// === DECLARATIVE COMPOSITION SYSTEM ===

// Command types for declarative animation sequences
enum CommandType {
  ANIMATE,      // Start animation on specified parts
  WAIT,         // Wait for specified duration
  WAIT_COMPLETE, // Wait for all active animations to complete
  DEBUG_FLASH   // Flash first and last LEDs of all parts
};

// Animation command structure
struct Command {
  CommandType cmd;
  uint8_t partMask;     // Bitmask: bit 0=Part1, bit 1=Part2, bit 2=Part3, bit 3=Part4
  AnimationType type;
  uint16_t duration;
};

// Composition definition - a sequence of commands
struct Composition {
  const char* name;
  const Command* commands;
  uint8_t commandCount;
  bool looping;
  uint32_t totalDuration;          // Total composition duration in ms (for documentation)
};

// Part bitmask definitions (1-indexed)
#define PART_1_MASK 0b0001  // Front A-Side
#define PART_2_MASK 0b0010  // Front B-Side  
#define PART_3_MASK 0b0100  // Back A-Side
#define PART_4_MASK 0b1000  // Back B-Side

#define FRONT_MASK  0b0011  // Parts 1 & 2
#define BACK_MASK   0b1100  // Parts 3 & 4
#define DIAGONAL_1_4_MASK 0b1001  // Parts 1 & 4 (diagonal - VALID)
#define DIAGONAL_2_3_MASK 0b0110  // Parts 2 & 3 (diagonal - VALID)
#define ALL_PARTS_MASK 0b1111  // All parts together (allowed for testing)

// === ANIMATION FRAMEWORK PROTOTYPES ===
void startAnimation(int partIndex, AnimationType type, uint16_t duration);
void updateAllAnimations();
void updatePartAnimation(int partIndex);
void clearAllParts();
bool isAnyPartActive();
void debugFlashFirstLastLEDs(uint8_t brightness);

// === BUTTON CONTROL FUNCTIONS ===
void handleButtonControl();

// === DECLARATIVE ANIMATION FUNCTIONS ===
bool isValidPartMask(uint8_t partMask);
void executeCommand(const Command& cmd);
void executeComposition(const Composition& comp);
void animatePartsFromMask(uint8_t partMask, AnimationType type, uint16_t duration);


// === DECLARATIVE ANIMATION IMPLEMENTATIONS ===

/*
 * Validate that a part mask doesn't violate the front/back constraint
 * Returns true if the mask is valid (never front and back simultaneously)
 * EXCEPTION: ALL_PARTS_MASK (0b1111) is allowed for testing flash patterns
 */
bool isValidPartMask(uint8_t partMask) {
  // Allow ALL_PARTS_MASK for testing purposes
  if (partMask == ALL_PARTS_MASK) {
    return true;
  }
  
  // Allow diagonal patterns (1&4 or 2&3)
  if (partMask == DIAGONAL_1_4_MASK || partMask == DIAGONAL_2_3_MASK) {
    return true;
  }
  
  bool hasFront = (partMask & FRONT_MASK) != 0;  // Check if any front parts (1&2)
  bool hasBack = (partMask & BACK_MASK) != 0;    // Check if any back parts (3&4)
  
  
  return !(hasFront && hasBack);  // Invalid if both front AND back are on
}

/*
 * Start animations on all parts specified in the bitmask
 * partMask uses 1-indexed representation: bit 0=Part1, bit 1=Part2, etc.
 */
void animatePartsFromMask(uint8_t partMask, AnimationType type, uint16_t duration) {
  for (int i = 0; i < NUM_PARTS; i++) {
    if (partMask & (1 << i)) {  // Check if bit i is set
      startAnimation(i, type, duration);  // i is 0-indexed for array access
    }
  }
}

/*
 * Execute a single animation command
 */
void executeCommand(const Command& cmd) {
  switch (cmd.cmd) {
    case ANIMATE:
      if (!isValidPartMask(cmd.partMask)) {
        // Skip invalid commands - could add error handling here
        return;
      }
      // Use fixed duration - tempo affects intensity, not timing
      animatePartsFromMask(cmd.partMask, cmd.type, cmd.duration);
      break;
      
    case WAIT:
      {
        // Use fixed delay with button checking every 10ms for responsiveness
        uint32_t waitStart = millis();
        while (millis() - waitStart < cmd.duration) {
          handleButtonControl();
          if (buttonPressed && currentState == PLAYING_COMPOSITION) {
            buttonPressed = false;
            currentState = IDLE;
            return; // Exit immediately to stop composition
          }
          delay(10);
        }
      }
      break;
      
    case WAIT_COMPLETE:
      while (isAnyPartActive()) {
        handleButtonControl();  // Keep button responsive during animation waits
        if (buttonPressed && currentState == PLAYING_COMPOSITION) {
          buttonPressed = false;
          currentState = IDLE;
          return; // Exit immediately to stop composition
        }
        updateAllAnimations();
        delay(10);
      }
      break;
      
    case DEBUG_FLASH:
      // Flash first and last LEDs of all parts
      // Use duration as brightness (0-255)
      debugFlashFirstLastLEDs((uint8_t)cmd.duration);
      break;
  }
}

/*
 * Execute a complete composition (sequence of commands)
 */
void executeComposition(const Composition& comp) {
  
  clearAllParts();
  
  do {
    for (int i = 0; i < comp.commandCount; i++) {
      // Check for button press before executing each command
      handleButtonControl();
      if (buttonPressed && currentState == PLAYING_COMPOSITION) {
        buttonPressed = false;
        currentState = IDLE;
        return; // Exit composition immediately
      }
      
      executeCommand(comp.commands[i]);
      
      // If executeCommand changed state to IDLE (button was pressed), exit
      if (currentState == IDLE) {
        return;
      }
      
      // Update animations during execution
      while (isAnyPartActive()) {
        handleButtonControl();  // Keep button responsive during animations
        if (buttonPressed && currentState == PLAYING_COMPOSITION) {
          buttonPressed = false;
          currentState = IDLE;
          return; // Exit composition immediately
        }
        updateAllAnimations();
        delay(10);
      }
    }
  } while (comp.looping && currentState == PLAYING_COMPOSITION);
}

// === ANIMATION DEFINITIONS ===


// Opposite Pairs: Front (1&2) together, then Back (3&4) together
const Command oppositePairsCommands[] = {
  {ANIMATE, FRONT_MASK, PULSE, 600},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, BACK_MASK, PULSE, 600},
  {WAIT_COMPLETE, 0, OFF, 0}
};


// Breathing Sequence: 1→2→3→4 individual breathing
const Command breathingSequenceCommands[] = {
  {ANIMATE, PART_1_MASK, BREATHE, 1200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, PART_2_MASK, BREATHE, 1200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, PART_3_MASK, BREATHE, 1200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, PART_4_MASK, BREATHE, 1200},
  {WAIT_COMPLETE, 0, OFF, 0}
};



// Composition definitions
const Composition oppositePairsComposition = {
  "Opposite Pairs", oppositePairsCommands, sizeof(oppositePairsCommands)/sizeof(Command), false, 0
};

const Composition breathingSequenceComposition = {
  "Breathing Sequence", breathingSequenceCommands, sizeof(breathingSequenceCommands)/sizeof(Command), false, 0
};

// compositions array moved after all definitions

// === COMPOSITION DEFINITIONS ===

// Demo composition that cycles through individual compositions would need to be restructured
// For now, we'll use individual compositions directly

// Friend composition - minimalist, intimate, acoustic (2:11 duration)
// Inspired by Ólafur Arnalds - "Saman"
// Balanced front/back lighting with wipe animations and proper state management
const Command friendCommands[] = {
  // Opening dialogue - front awakening (0:00-0:20)
  {ANIMATE, PART_1_MASK, WIPE_IN_FROM_BOTTOM, 3500},  // Part 1 awakens (OFF→ON)
  {WAIT, 0, OFF, 600},                                 // Brief pause
  {ANIMATE, PART_2_MASK, WIPE_IN_FROM_BOTTOM, 3500},  // Part 2 joins front (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},                                // Front pair lit
  
  // Completing the frame - back awakening (0:20-0:40)
  {ANIMATE, FRONT_MASK, FADE_OUT, 2400},              // Front fades out (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},                                 // Brief darkness
  {ANIMATE, PART_3_MASK, WIPE_IN_FROM_TOP, 3600},     // Part 3 awakens (OFF→ON)
  {WAIT, 0, OFF, 800},
  {ANIMATE, PART_4_MASK, WIPE_IN_FROM_TOP, 3600},     // Part 4 completes back (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2200},                                // Back pair lit - harmony
  
  // Alternating conversation - front/back dialogue (0:40-1:05)
  {ANIMATE, BACK_MASK, WIPE_OUT_FROM_TOP, 2400},      // Back speaks by leaving (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},                                 // Brief darkness
  {ANIMATE, FRONT_MASK, WIPE_IN_FROM_BOTTOM, 2500},   // Front returns (OFF→ON)
  {WAIT, 0, OFF, 500},                                 // Front lit
  {ANIMATE, FRONT_MASK, WIPE_OUT_FROM_BOTTOM, 2400},  // Front leaves (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},                                 // Brief darkness
  {ANIMATE, BACK_MASK, WIPE_IN_FROM_TOP, 2500},       // Back returns (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1700},                                // Back lit - contemplation
  
  // Individual expressions - sequential solos (1:05-1:20)
  {ANIMATE, BACK_MASK, FADE_OUT, 800},                // Clear back first (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {ANIMATE, PART_1_MASK, BREATHE, 2800},              // Part 1 breathes alone (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 400},
  {ANIMATE, PART_2_MASK, BREATHE, 2800},              // Part 2 breathes alone (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  
  // Diagonal patterns - new feature (1:20-1:35)
  {ANIMATE, DIAGONAL_1_4_MASK, PULSE, 2200},          // Parts 1&4 diagonal pulse (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 400},
  {ANIMATE, DIAGONAL_2_3_MASK, PULSE, 2200},          // Parts 2&3 diagonal pulse (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, DIAGONAL_1_4_MASK, BREATHE, 2600},        // Parts 1&4 diagonal breathe (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 350},
  {ANIMATE, DIAGONAL_2_3_MASK, BREATHE, 2600},        // Parts 2&3 diagonal breathe (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800},                                 // Diagonal patterns complete
  
  // Wave pattern - sequential individual parts (1:35-1:42)
  {ANIMATE, PART_1_MASK, WIPE_OUT_FROM_TOP, 1200},    // Wave starts (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_2_MASK, WIPE_OUT_FROM_TOP, 1200},    // Wave continues (OFF→OFF, no-op)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_3_MASK, WIPE_OUT_FROM_BOTTOM, 1200}, // Wave to back (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_4_MASK, WIPE_OUT_FROM_BOTTOM, 1200}, // Wave completes (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 600},                                 // Brief all-dark pause
  
  // Final emergence - front then back awakening (1:42-2:05)
  {ANIMATE, FRONT_MASK, WIPE_IN_FROM_BOTTOM, 3000},   // Front rises (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1400},                                // Front moment
  {ANIMATE, FRONT_MASK, FADE_OUT, 1500},              // Front fades (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 600},                                 // Brief silence
  {ANIMATE, BACK_MASK, WIPE_IN_FROM_TOP, 1500},       // Back descends (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2000},                                // Extended back moment
  
  // Final dialogue - gentle alternation (2:05-2:11)
  {ANIMATE, BACK_MASK, BREATHE, 2200},                // Back breathes (ON→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},                                 // Pause
  {ANIMATE, BACK_MASK, FADE_OUT, 1000},               // Back fades (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},                                 // Brief darkness
  {ANIMATE, FRONT_MASK, FADE_IN, 1400},               // Front returns gently (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},                                // Front contemplation
  {ANIMATE, FRONT_MASK, PULSE, 1800},                 // Front pulses farewell (ON→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 600},                                 // Final pause
  {ANIMATE, FRONT_MASK, FADE_OUT, 2200},              // Final fade to silence (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1200}                                 // Extended silence
};

// Friend composition - intimate, acoustic feeling  
const Composition friendCompositionSaman = {
  "Friend - Saman",
  friendCommands,
  sizeof(friendCommands)/sizeof(Command),
  true,         // Loop indefinitely  
  131000        // Precise duration: 2:11 = 131 seconds
};


// === NEW COMPOSITION PATTERNS ===

// Mirror Effect - Alternating diagonal patterns enhancing doppelganger concept
const Command mirrorEffectCommands[] = {
  {ANIMATE, DIAGONAL_1_4_MASK, FADE_IN, 600},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, DIAGONAL_1_4_MASK, FADE_OUT, 400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {ANIMATE, DIAGONAL_2_3_MASK, FADE_IN, 600},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, DIAGONAL_2_3_MASK, FADE_OUT, 400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, DIAGONAL_1_4_MASK, PULSE, 800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, DIAGONAL_2_3_MASK, PULSE, 800},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Heartbeat - Double-pulse pattern moving between front and back
const Command heartbeatCommands[] = {
  {ANIMATE, FRONT_MASK, PULSE, 250},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, FRONT_MASK, PULSE, 250},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800},
  {ANIMATE, BACK_MASK, PULSE, 250},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, BACK_MASK, PULSE, 250},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800}
};

// Conversation - Individual parts taking turns with varying durations
const Command conversationCommands[] = {
  {ANIMATE, PART_1_MASK, FLUTTER, 1200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, PART_3_MASK, FLUTTER, 800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_2_MASK, FLUTTER, 1500},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 400},
  {ANIMATE, PART_4_MASK, FLUTTER, 600},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, PART_1_MASK, FLUTTER, 900},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_3_MASK, FLUTTER, 1100},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Echo - One side triggers, opposite responds with diminished intensity
const Command echoCommands[] = {
  {ANIMATE, FRONT_MASK, CASCADE, 1000},
  {WAIT, 0, OFF, 200},
  {ANIMATE, BACK_MASK, SHIMMER, 800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, PART_3_MASK, CASCADE, 1000},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_1_MASK, SHIMMER, 800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, PART_2_MASK, CASCADE, 1000},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_4_MASK, SHIMMER, 800},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Tidal - Slow wipe patterns that ebb and flow like ocean waves
const Command tidalCommands[] = {
  {ANIMATE, FRONT_MASK, WIPE_IN_FROM_BOTTOM, 2000},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800},
  {ANIMATE, FRONT_MASK, WIPE_OUT_FROM_TOP, 1800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 600},
  {ANIMATE, BACK_MASK, WIPE_IN_FROM_TOP, 2200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 900},
  {ANIMATE, BACK_MASK, WIPE_OUT_FROM_BOTTOM, 1900},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 700}
};

// Contemplation - Extended breathe cycles with long pauses
const Command contemplationCommands[] = {
  {ANIMATE, PART_1_MASK, BREATHE, 3000},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2000},
  {ANIMATE, PART_3_MASK, BREATHE, 3200},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2200},
  {ANIMATE, PART_2_MASK, BREATHE, 2800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1800},
  {ANIMATE, PART_4_MASK, BREATHE, 3400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2500}
};

// Recognition - Quick individual flashes as parts "notice" each other
const Command recognitionCommands[] = {
  {ANIMATE, PART_1_MASK, STROBE, 400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_3_MASK, STROBE, 300},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_2_MASK, STROBE, 350},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 150},
  {ANIMATE, PART_4_MASK, STROBE, 300},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 300},
  {ANIMATE, DIAGONAL_1_4_MASK, STROBE, 500},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, DIAGONAL_2_3_MASK, STROBE, 500},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Farewell - Gradual cascade where each part dims in sequence
const Command farewellCommands[] = {
  {ANIMATE, PART_1_MASK, FADE_IN, 500},
  {ANIMATE, PART_2_MASK, FADE_IN, 500},
  {ANIMATE, PART_3_MASK, FADE_IN, 500},
  {ANIMATE, PART_4_MASK, FADE_IN, 500},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},
  {ANIMATE, PART_1_MASK, FADE_OUT, 800},
  {WAIT, 0, OFF, 300},
  {ANIMATE, PART_2_MASK, FADE_OUT, 800},
  {WAIT, 0, OFF, 300},
  {ANIMATE, PART_3_MASK, FADE_OUT, 800},
  {WAIT, 0, OFF, 300},
  {ANIMATE, PART_4_MASK, FADE_OUT, 800},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1500}
};

// Awakening - Reverse cascade where parts illuminate in morning-like progression
const Command awakeningCommands[] = {
  {ANIMATE, PART_4_MASK, SPIRAL, 1500},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_3_MASK, SPIRAL, 1500},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_2_MASK, SPIRAL, 1500},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_1_MASK, SPIRAL, 1500},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800},
  {ANIMATE, ALL_PARTS_MASK, TWINKLE, 2000},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000}
};

// Composition definitions for new patterns
const Composition mirrorEffectComposition = {
  "Mirror Effect", mirrorEffectCommands, sizeof(mirrorEffectCommands)/sizeof(Command), false, 0
};

const Composition heartbeatComposition = {
  "Heartbeat", heartbeatCommands, sizeof(heartbeatCommands)/sizeof(Command), false, 0
};

const Composition conversationComposition = {
  "Conversation", conversationCommands, sizeof(conversationCommands)/sizeof(Command), false, 0
};

const Composition echoComposition = {
  "Echo", echoCommands, sizeof(echoCommands)/sizeof(Command), false, 0
};

const Composition tidalComposition = {
  "Tidal", tidalCommands, sizeof(tidalCommands)/sizeof(Command), false, 0
};

const Composition contemplationComposition = {
  "Contemplation", contemplationCommands, sizeof(contemplationCommands)/sizeof(Command), false, 0
};

const Composition recognitionComposition = {
  "Recognition", recognitionCommands, sizeof(recognitionCommands)/sizeof(Command), false, 0
};

const Composition farewellComposition = {
  "Farewell", farewellCommands, sizeof(farewellCommands)/sizeof(Command), false, 0
};

const Composition awakeningComposition = {
  "Awakening", awakeningCommands, sizeof(awakeningCommands)/sizeof(Command), false, 0
};

// === COMPOSITIONS ARRAY ===
const Composition* compositions[] = {
  &oppositePairsComposition,
  &breathingSequenceComposition,
  &friendCompositionSaman,
  &mirrorEffectComposition,
  &heartbeatComposition,
  &conversationComposition,
  &echoComposition,
  &tidalComposition,
  &contemplationComposition,
  &recognitionComposition,
  &farewellComposition,
  &awakeningComposition
};

#define NUM_COMPOSITIONS (sizeof(compositions)/sizeof(Composition*))

// === EASING FUNCTIONS ===

/*
 * Easing functions provide natural motion curves instead of linear progression
 * All functions take a progress value from 0.0 to 1.0 and return an eased value
 */


// Ease in-out (slow start and end, fast middle)
float easeInOut(float t) {
  if (t < 0.5) {
    return 4.0 * t * t * t;  // First half: ease-in
  } else {
    float f = 2.0 * t - 2.0;
    return 1.0 + f * f * f / 2.0;  // Second half: ease-out
  }
}

// === ANIMATION FRAMEWORK IMPLEMENTATION ===

/*
 * Start an animation on a single part
 * 
 * partIndex: Which part to animate (0-3, maps to Parts 1-4 in bitmask system)
 * type: What kind of animation to run
 * duration: How long the animation should take in milliseconds
 * 
 * NOTE: This function uses 0-indexed arrays internally but the declarative
 * system uses 1-indexed bitmasks. partIndex 0 = Part 1, partIndex 1 = Part 2, etc.
 * 
 * This is the fundamental building block - the declarative system calls this
 * via animatePartsFromMask() to start individual part animations.
 */
void startAnimation(int partIndex, AnimationType type, uint16_t duration) {
  if (partIndex >= NUM_PARTS) return;
  
  LEDPart* part = &parts[partIndex];
  part->animation.type = type;
  part->animation.startTime = millis();
  part->animation.duration = duration;
  part->animation.isActive = true;
}


/*
 * Update all active animations and refresh the LED strips
 * 
 * This function should be called frequently (every 10ms) in your main loop
 * when animations are running. It calculates the current brightness for each
 * part based on animation progress and updates the physical LEDs.
 */
void updateAllAnimations() {
  for (int i = 0; i < NUM_PARTS; i++) {
    updatePartAnimation(i);
  }
  
  // Refresh all strips to show the updated colors
  strip1.show();
  strip2.show();
  strip3.show();
  strip4.show();
}

/*
 * Update a single part's animation
 * 
 * This handles the math for each animation type, calculating the current
 * brightness based on elapsed time and animation progress.
 */
void updatePartAnimation(int partIndex) {
  LEDPart* part = &parts[partIndex];
  
  if (!part->animation.isActive) return;
  
  uint32_t elapsed = millis() - part->animation.startTime;
  
  // Check if animation is complete
  if (elapsed >= part->animation.duration) {
    part->animation.isActive = false;
    
    // Ensure final state is correct based on animation type
    switch (part->animation.type) {
      case FADE_OUT:
      case OFF:
      case PULSE:
      case BREATHE:
      case WIPE_OUT_FROM_TOP:
      case WIPE_OUT_FROM_BOTTOM:
      case SHIMMER:
      case TWINKLE:
      case FLUTTER:
      case CASCADE:
      case STROBE:
        part->currentBrightness = 0;
        // Set all LEDs to off
        for (int i = part->startLED; i <= part->endLED; i++) {
          part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, 0));
        }
        break;
        
      case FADE_IN:
      case WIPE_IN_FROM_TOP:
      case WIPE_IN_FROM_BOTTOM:
      case SPIRAL:
        part->currentBrightness = 255;
        // Set all LEDs to full brightness
        for (int i = part->startLED; i <= part->endLED; i++) {
          part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, 255));
        }
        break;
    }
    return;
  }
  
  // Calculate progress (0.0 to 1.0)
  float progress = (float)elapsed / (float)part->animation.duration;
  uint8_t brightness = 0;
  
  // Calculate brightness based on animation type and progress
  switch (part->animation.type) {
    case FADE_IN:
      brightness = (uint8_t)(255 * progress);
      break;
      
    case FADE_OUT:
      brightness = (uint8_t)(255 * (1.0 - progress));
      // Ensure we reach true zero at the end
      if (progress >= 0.98) brightness = 0;
      break;
      
    case PULSE:
      // Pulse: fade in for first half, fade out for second half
      if (progress <= 0.5) {
        brightness = (uint8_t)(255 * (progress * 2));
      } else {
        brightness = (uint8_t)(255 * (2 - progress * 2));
        // Ensure we reach true zero at the end
        if (progress >= 0.98) brightness = 0;
      }
      break;
      
    case BREATHE:
      // Breathe: slow inhale (70% of time), quick exhale (30% of time)
      // This mimics natural breathing rhythm
      if (progress <= 0.7) {
        // Inhale phase - slow and steady
        brightness = (uint8_t)(255 * (progress / 0.7));
      } else {
        // Exhale phase - quick release
        float exhaleProgress = (progress - 0.7) / 0.3;
        brightness = (uint8_t)(255 * (1.0 - exhaleProgress));
        // Ensure we reach true zero at the end
        if (progress >= 0.98) brightness = 0;
      }
      break;
      
    case WIPE_IN_FROM_TOP:
    case WIPE_IN_FROM_BOTTOM:
    case WIPE_OUT_FROM_TOP:
    case WIPE_OUT_FROM_BOTTOM:
    case SPIRAL:
      // Wipe and spiral animations are handled LED by LED, brightness set to full
      brightness = 255;
      break;
      
    case SHIMMER:
      // Rapid micro-pulses with varying intensity
      {
        float shimmerFreq = 8.0; // 8 pulses per animation duration
        float shimmerPhase = progress * shimmerFreq * 2.0 * PI;
        float shimmerBase = 0.3 + 0.7 * sin(shimmerPhase); // Varies 0.3-1.0
        float shimmerNoise = 0.8 + 0.2 * sin(shimmerPhase * 1.7); // Add complexity
        brightness = (uint8_t)(255 * shimmerBase * shimmerNoise * (1.0 - progress * 0.2)); // Slight decay
      }
      break;
      
    case TWINKLE:
      // Individual LEDs randomly fade in/out - handled per LED below
      brightness = 255;
      break;
      
    case FLUTTER:
      // Organic candlelight-like variations
      {
        float flutter1 = sin(progress * 12.0 * PI + 0.3);
        float flutter2 = sin(progress * 8.7 * PI + 1.2);
        float flutter3 = sin(progress * 15.3 * PI + 2.1);
        float flutterBase = 0.4 + 0.6 * ((flutter1 + flutter2 * 0.7 + flutter3 * 0.4) / 2.1);
        brightness = (uint8_t)(255 * max(0.0, min(1.0, flutterBase)));
      }
      break;
      
    case CASCADE:
      // Multi-stage wave with building intensity
      {
        if (progress <= 0.3) {
          // Build phase
          float buildPhase = progress / 0.3;
          brightness = (uint8_t)(255 * buildPhase * buildPhase);
        } else if (progress <= 0.7) {
          // Peak phase
          float peakPhase = (progress - 0.3) / 0.4;
          float peakIntensity = 1.0 + 0.5 * sin(peakPhase * 6.0 * PI);
          brightness = (uint8_t)(255 * min(1.0, peakIntensity));
        } else {
          // Release phase
          float releasePhase = (progress - 0.7) / 0.3;
          brightness = (uint8_t)(255 * (1.0 - releasePhase * releasePhase));
        }
      }
      break;
      
    case STROBE:
      // Precise rhythmic flashing
      {
        float strobeFreq = 6.0; // 6 flashes per animation duration
        float strobePhase = progress * strobeFreq;
        float strobeCycle = strobePhase - floor(strobePhase);
        brightness = (strobeCycle < 0.3) ? 255 : 0; // 30% on, 70% off per cycle
      }
      break;
      
    case OFF:
      brightness = 0;
      break;
  }
  
  part->currentBrightness = brightness;
  
  // Handle special animations LED by LED, others apply to all LEDs
  if (part->animation.type == WIPE_IN_FROM_TOP || 
      part->animation.type == WIPE_IN_FROM_BOTTOM ||
      part->animation.type == WIPE_OUT_FROM_TOP || 
      part->animation.type == WIPE_OUT_FROM_BOTTOM ||
      part->animation.type == SPIRAL ||
      part->animation.type == TWINKLE) {
    
    int totalLEDs = part->endLED - part->startLED + 1;
    
    // Clear all LEDs first
    for (int i = part->startLED; i <= part->endLED; i++) {
      part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, 0));
    }
    
    if (part->animation.type == TWINKLE) {
      // TWINKLE: Individual LEDs randomly fade in/out like stars
      for (int i = 0; i < totalLEDs; i++) {
        int ledIndex = part->startLED + i;
        // Use LED index as seed for consistent but varied behavior per LED
        float ledSeed = (float)(ledIndex * 7 + 13) / 37.0; // Pseudo-random seed
        float ledPhase = fmod(ledSeed + progress * 3.0, 1.0); // Different timing per LED
        
        // Create sparkle effect with varying intensities
        float twinkleIntensity = 0.0;
        if (ledPhase < 0.1) {
          // Quick fade in
          twinkleIntensity = ledPhase / 0.1;
        } else if (ledPhase < 0.3) {
          // Hold bright
          twinkleIntensity = 1.0;
        } else if (ledPhase < 0.5) {
          // Quick fade out
          twinkleIntensity = (0.5 - ledPhase) / 0.2;
        }
        // else stay dark (50% of cycle is dark)
        
        // Add randomness to which LEDs are active
        float activation = sin(ledSeed * 6.28 + progress * 4.0) * 0.5 + 0.5;
        if (activation > 0.7) { // Only 30% of LEDs active at any time
          uint8_t ledBrightness = (uint8_t)(255 * twinkleIntensity);
          part->strip->setPixelColor(ledIndex, part->strip->Color(0, 0, 0, ledBrightness));
        }
      }
    } else if (part->animation.type == SPIRAL) {
      // SPIRAL: Wipe with varying speeds creating spiral effect
      float spiralFreq = 2.0; // Two spiral waves
      for (int i = 0; i < totalLEDs; i++) {
        float ledPosition = (float)i / (totalLEDs - 1); // 0.0 to 1.0
        float spiralOffset = sin(ledPosition * spiralFreq * 2.0 * PI) * 0.2; // Spiral delay
        float ledProgress = progress - spiralOffset;
        
        if (ledProgress > 0.0 && ledProgress < 1.0) {
          float ledIntensity = sin(ledProgress * PI); // Sine wave intensity
          uint8_t ledBrightness = (uint8_t)(255 * ledIntensity);
          int ledIndex = part->startLED + i;
          part->strip->setPixelColor(ledIndex, part->strip->Color(0, 0, 0, ledBrightness));
        }
      }
    } else {
      // Original wipe animations
      // Apply symmetrical easing to all wipe animations for consistent, natural movement
      float easedProgress = easeInOut(progress);  // Symmetrical: slow start, fast middle, slow end
      
      // Calculate position with sub-LED precision for smooth fading
      float exactPosition = totalLEDs * easedProgress;
      int activeLEDs = (int)exactPosition;
      float fadeAmount = exactPosition - activeLEDs;  // Fractional part for fade
    
      // Light LEDs based on wipe direction and type with fading
      for (int i = 0; i <= activeLEDs && i < totalLEDs; i++) {
        int ledIndex;
        uint8_t ledBrightness = 0;
        bool isWipeIn = (part->animation.type == WIPE_IN_FROM_TOP || part->animation.type == WIPE_IN_FROM_BOTTOM);
        
        // Calculate LED index based on direction
        switch (part->animation.type) {
          case WIPE_IN_FROM_TOP:
          case WIPE_OUT_FROM_TOP:
            ledIndex = part->startLED + i;
            break;
            
          case WIPE_IN_FROM_BOTTOM:
          case WIPE_OUT_FROM_BOTTOM:
            ledIndex = part->endLED - i;
            break;
            
          default:
            continue;
        }
        
        // Skip if LED index is out of bounds
        if (ledIndex < part->startLED || ledIndex > part->endLED) continue;
        
        // Calculate brightness with fading
        if (i < activeLEDs) {
          // Fully lit LEDs
          ledBrightness = isWipeIn ? 255 : 0;
        } else if (i == activeLEDs && fadeAmount > 0) {
          // Fading LED at the edge
          if (isWipeIn) {
            ledBrightness = (uint8_t)(255 * fadeAmount);
          } else {
            ledBrightness = (uint8_t)(255 * (1.0 - fadeAmount));
          }
        }
        
        part->strip->setPixelColor(ledIndex, part->strip->Color(0, 0, 0, ledBrightness));
      }
      
      // For wipe out animations, light the remaining unwiped LEDs
      if (part->animation.type == WIPE_OUT_FROM_TOP || part->animation.type == WIPE_OUT_FROM_BOTTOM) {
        for (int i = activeLEDs + 1; i < totalLEDs; i++) {
          int ledIndex;
          
          if (part->animation.type == WIPE_OUT_FROM_TOP) {
            ledIndex = part->startLED + i;
          } else {  // WIPE_OUT_FROM_BOTTOM
            ledIndex = part->endLED - i;
          }
          
          if (ledIndex >= part->startLED && ledIndex <= part->endLED) {
            part->strip->setPixelColor(ledIndex, part->strip->Color(0, 0, 0, 255));
          }
        }
      }
    }
    
  } else {
    // Standard animations: apply brightness to all LEDs in the part
    // Using white channel (4th parameter) for GRBW strips
    for (int i = part->startLED; i <= part->endLED; i++) {
      part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, brightness));
    }
  }
}

/*
 * Turn off all parts and clear any active animations
 * 
 * Call this at the start of each animation pattern to ensure
 * a clean starting state.
 */
void clearAllParts() {
  for (int i = 0; i < NUM_PARTS; i++) {
    parts[i].animation.isActive = false;
    parts[i].currentBrightness = 0;
    
    // Explicitly set all LEDs to completely off
    for (int j = parts[i].startLED; j <= parts[i].endLED; j++) {
      parts[i].strip->setPixelColor(j, parts[i].strip->Color(0, 0, 0, 0));
    }
  }
  
  // Force update all strips to ensure LEDs are off
  strip1.show();
  strip2.show();
  strip3.show();
  strip4.show();
  
  // Small delay to ensure the command is processed
  delay(10);
}

/*
 * Check if any part is currently running an animation
 * 
 * Use this in while loops to wait for animations to complete:
 *   while (isAnyPartActive()) {
 *     updateAllAnimations();
 *     delay(10);
 *   }
 */
bool isAnyPartActive() {
  for (int i = 0; i < NUM_PARTS; i++) {
    if (parts[i].animation.isActive) return true;
  }
  return false;
}

/*
 * Debug function: Flash only the first and last LED of each part
 * Used for testing to identify part boundaries
 */
void debugFlashFirstLastLEDs(uint8_t brightness) {
  for (int i = 0; i < NUM_PARTS; i++) {
    LEDPart* part = &parts[i];
    
    // Light first LED (startLED)
    part->strip->setPixelColor(part->startLED, part->strip->Color(0, 0, 0, brightness));
    
    // Light last LED (endLED) 
    part->strip->setPixelColor(part->endLED, part->strip->Color(0, 0, 0, brightness));
    
    // Turn off all LEDs in between
    for (int j = part->startLED + 1; j < part->endLED; j++) {
      part->strip->setPixelColor(j, part->strip->Color(0, 0, 0, 0));
    }
  }
  
  // Update all strips
  strip1.show();
  strip2.show();
  strip3.show();
  strip4.show();
}

// === IDLE STATE IMPLEMENTATION ===

/*
 * Idle state with all lights off
 * Keeps all LEDs turned off while waiting for button press
 */
void updateIdleState() {
  if (idleStartTime == 0) {
    idleStartTime = millis();
  }
  
  // Keep all LEDs off in idle mode
  for (int i = 0; i < NUM_PARTS; i++) {
    LEDPart* part = &parts[i];
    for (int j = part->startLED; j <= part->endLED; j++) {
      part->strip->setPixelColor(j, part->strip->Color(0, 0, 0, 0));
    }
  }
  
  // Update all strips to ensure they remain off
  strip1.show();
  strip2.show();
  strip3.show();
  strip4.show();
}

// === BUTTON CONTROL IMPLEMENTATION ===

/*
 * Handle button input and control LED
 * Detects button presses to trigger composition playback
 */
void handleButtonControl() {
  buttonState = digitalRead(BUTTON_PIN);
  
  // Control LED based on system state
  if (currentState == IDLE) {
    digitalWrite(CONTROL_LED_PIN, HIGH);  // LED on during idle
  } else {
    digitalWrite(CONTROL_LED_PIN, LOW);   // LED off during composition
  }
  
  // Detect button press (transition from HIGH to LOW)
  if (buttonState != lastButtonState) {
    if (buttonState == LOW && lastButtonState == HIGH) {
      // Button was just pressed
      buttonPressed = true;
    }
    lastButtonState = buttonState;
  }
}

// === SETUP ===
void setup()
{
  // Initialize all LED strips
  strip1.begin();
  strip1.setBrightness(BRIGHTNESS);
  strip1.show();

  strip2.begin();
  strip2.setBrightness(BRIGHTNESS);
  strip2.show();

  strip3.begin();
  strip3.setBrightness(BRIGHTNESS);
  strip3.show();

  strip4.begin();
  strip4.setBrightness(BRIGHTNESS);
  strip4.show();
  
  // Initialize button and control LED
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(CONTROL_LED_PIN, OUTPUT);
  
  // Start with all parts off
  clearAllParts();
}

// === MAIN LOOP ===
void loop()
{
  // Handle button control continuously
  handleButtonControl();
  
  switch (currentState) {
    case IDLE:
      // Update idle state with gentle pulsing
      updateIdleState();
      
      // Check if button was pressed to start composition
      if (buttonPressed) {
        buttonPressed = false; // Clear the flag
        currentState = PLAYING_COMPOSITION;
        clearAllParts(); // Clear idle state before starting composition
        delay(50); // Brief pause for clean transition
      }
      
      // Small delay to prevent overwhelming the processor
      delay(20);
      break;
      
    case PLAYING_COMPOSITION:
      // Play current composition from the array
      executeComposition(*compositions[currentCompositionIndex]);
      
      // Only reach here if composition completed naturally or was interrupted
      if (currentState == PLAYING_COMPOSITION) {
        // Composition completed naturally, move to next one
        currentCompositionIndex = (currentCompositionIndex + 1) % NUM_COMPOSITIONS;
        clearAllParts(); // Clean transition between compositions
        delay(500); // Brief pause between compositions
      } else {
        // Composition was interrupted by button press (state changed to IDLE)
        idleStartTime = 0; // Reset idle timing
        clearAllParts(); // Ensure clean transition to idle
        delay(100); // Brief pause before starting idle
      }
      break;
  }
}
