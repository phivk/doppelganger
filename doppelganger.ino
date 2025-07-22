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
 * - Any front + back combination is forbidden and automatically validated
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

// === HARDWARE CONFIGURATION ===
#define PIN1 2              // Strip 1 data pin
#define PIN2 3              // Strip 2 data pin
#define PIN3 4              // Strip 3 data pin
#define PIN4 5              // Strip 4 data pin
#define NUM_LEDS 60         // LEDs per strip
#define BRIGHTNESS 50       // Global brightness (0-255)

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
  WIPE_OUT_FROM_BOTTOM   // wipe out from bottom to top
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

// === PRECISE DURATION SYSTEM ===
// Simple system for exact compositional timing control

// Global composition timing tracking (optional)
uint32_t compositionStartTime = 0;
uint32_t compositionTotalDuration = 0;

// Start composition timing tracking
void startCompositionTiming(uint32_t totalDuration) {
  compositionStartTime = millis();
  compositionTotalDuration = totalDuration;
}

// Get elapsed time in current composition (for debugging/monitoring)
uint32_t getCompositionElapsed() {
  if (compositionStartTime == 0) return 0;
  return millis() - compositionStartTime;
}

// Check if composition should be complete (for validation)
bool isCompositionTimeComplete() {
  if (compositionStartTime == 0 || compositionTotalDuration == 0) return false;
  return getCompositionElapsed() >= compositionTotalDuration;
}

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

// Composition definition - a sequence of commands with precise duration control
struct Composition {
  const char* name;
  const Command* commands;
  uint8_t commandCount;
  bool looping;
  uint32_t totalDuration;          // Total composition duration in ms (0 = no duration control)
};

// Part bitmask definitions (1-indexed)
#define PART_1_MASK 0b0001  // Front A-Side
#define PART_2_MASK 0b0010  // Front B-Side  
#define PART_3_MASK 0b0100  // Back A-Side
#define PART_4_MASK 0b1000  // Back B-Side

#define FRONT_MASK  0b0011  // Parts 1 & 2
#define BACK_MASK   0b1100  // Parts 3 & 4
#define A_SIDE_MASK 0b0101  // Parts 1 & 3 (INVALID - violates constraint)
#define B_SIDE_MASK 0b1010  // Parts 2 & 4 (INVALID - violates constraint)
#define ALL_PARTS_MASK 0b1111  // All parts together (allowed for testing)

// === ANIMATION FRAMEWORK PROTOTYPES ===
void startAnimation(int partIndex, AnimationType type, uint16_t duration);
void updateAllAnimations();
void updatePartAnimation(int partIndex);
void clearAllParts();
bool isAnyPartActive();
void debugFlashFirstLastLEDs(uint8_t brightness);

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
      // Use fixed delay - no tempo adjustment to timing
      delay(cmd.duration);
      break;
      
    case WAIT_COMPLETE:
      while (isAnyPartActive()) {
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
  // Start composition timing if specified
  if (comp.totalDuration > 0) {
    startCompositionTiming(comp.totalDuration);
  }
  
  clearAllParts();
  
  do {
    for (int i = 0; i < comp.commandCount; i++) {
      executeCommand(comp.commands[i]);
      
      // Update animations during execution
      while (isAnyPartActive()) {
        updateAllAnimations();
        delay(10);
      }
    }
  } while (comp.looping);
}

// === ANIMATION DEFINITIONS ===

// Wave Pattern: Front wave (1→2), then Back wave (3→4)
const Command waveCommands[] = {
  {ANIMATE, PART_1_MASK, FADE_IN, 400},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_2_MASK, FADE_IN, 400},
  {ANIMATE, FRONT_MASK, FADE_OUT, 400},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_3_MASK, FADE_IN, 400},
  {WAIT, 0, OFF, 100},
  {ANIMATE, PART_4_MASK, FADE_IN, 400},
  {ANIMATE, BACK_MASK, FADE_OUT, 400}
};

// Opposite Pairs: Front (1&2) together, then Back (3&4) together
const Command oppositePairsCommands[] = {
  {ANIMATE, FRONT_MASK, PULSE, 600},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 500},
  {ANIMATE, BACK_MASK, PULSE, 600},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// All Together: Rapid alternation between front and back
const Command allTogetherCommands[] = {
  {ANIMATE, FRONT_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100},
  {ANIMATE, 0, FADE_OUT, 50},  // Clear all
  {ANIMATE, BACK_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100},
  {ANIMATE, 0, FADE_OUT, 50},  // Clear all
  {ANIMATE, FRONT_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100},
  {ANIMATE, 0, FADE_OUT, 50},  // Clear all
  {ANIMATE, BACK_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100},
  {ANIMATE, 0, FADE_OUT, 50},  // Clear all
  {ANIMATE, FRONT_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100},
  {ANIMATE, 0, FADE_OUT, 50},  // Clear all
  {ANIMATE, BACK_MASK, FADE_IN, 100},
  {WAIT, 0, OFF, 100}
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

// Chase Pattern: 1→2→3→4 with overlapping fades
const Command chasePatternCommands[] = {
  {ANIMATE, PART_1_MASK, FADE_IN, 250},
  {WAIT, 0, OFF, 125},
  {ANIMATE, PART_1_MASK, FADE_OUT, 250},
  {ANIMATE, PART_2_MASK, FADE_IN, 250},
  {WAIT, 0, OFF, 125},
  {ANIMATE, PART_2_MASK, FADE_OUT, 250},
  {ANIMATE, PART_3_MASK, FADE_IN, 250},
  {WAIT, 0, OFF, 125},
  {ANIMATE, PART_3_MASK, FADE_OUT, 250},
  {ANIMATE, PART_4_MASK, FADE_IN, 250},
  {WAIT, 0, OFF, 125},
  {ANIMATE, PART_4_MASK, FADE_OUT, 250},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Doppelganger Pattern: Showcases front/back alternation
const Command doppelgangerPatternCommands[] = {
  {ANIMATE, FRONT_MASK, PULSE, 400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, BACK_MASK, PULSE, 400},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_1_MASK, PULSE, 267},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_3_MASK, PULSE, 267},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_2_MASK, PULSE, 267},
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_4_MASK, PULSE, 267},
  {WAIT_COMPLETE, 0, OFF, 0}
};

// Composition definitions
const Composition waveComposition = {
  "Wave", waveCommands, sizeof(waveCommands)/sizeof(Command), false, 0
};

const Composition oppositePairsComposition = {
  "Opposite Pairs", oppositePairsCommands, sizeof(oppositePairsCommands)/sizeof(Command), false, 0
};

const Composition allTogetherComposition = {
  "All Together", allTogetherCommands, sizeof(allTogetherCommands)/sizeof(Command), false, 0
};

const Composition breathingSequenceComposition = {
  "Breathing Sequence", breathingSequenceCommands, sizeof(breathingSequenceCommands)/sizeof(Command), false, 0
};

const Composition chasePatternComposition = {
  "Chase Pattern", chasePatternCommands, sizeof(chasePatternCommands)/sizeof(Command), false, 0
};

const Composition doppelgangerComposition = {
  "Doppelganger", doppelgangerPatternCommands, sizeof(doppelgangerPatternCommands)/sizeof(Command), false, 0
};

const Composition* compositions[] = {
  &waveComposition,
  &oppositePairsComposition,
  &allTogetherComposition,
  &breathingSequenceComposition,
  &chasePatternComposition,
  &doppelgangerComposition
};

#define NUM_COMPOSITIONS (sizeof(compositions)/sizeof(Composition*))

// === COMPOSITION DEFINITIONS ===

// Demo composition that cycles through individual compositions would need to be restructured
// For now, we'll use individual compositions directly

// Friend composition - minimalist, intimate, acoustic (2:11 duration)
// Inspired by Ólafur Arnalds - "Saman"
// Balanced front/back lighting with wipe animations and proper state management
const Command friendCommands[] = {
  // Opening dialogue - both sides awaken together (0:00-0:24)
  {ANIMATE, PART_1_MASK, WIPE_IN_FROM_BOTTOM, 4000},  // Part 1 awakens (OFF→ON)
  {WAIT, 0, OFF, 800},                                 // Brief pause
  {ANIMATE, PART_3_MASK, WIPE_IN_FROM_BOTTOM, 4000},  // Part 3 responds quickly (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1200},                                // Both sides lit
  
  // Completing the frame - other parts join (0:24-0:48)
  {ANIMATE, PART_2_MASK, WIPE_IN_FROM_TOP, 4200},     // Part 2 joins (OFF→ON)
  {WAIT, 0, OFF, 1000},
  {ANIMATE, PART_4_MASK, WIPE_IN_FROM_TOP, 4200},     // Part 4 completes (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2600},                                // All parts lit - harmony
  
  // Alternating conversation - front/back dialogue (0:48-1:18)
  {ANIMATE, FRONT_MASK, WIPE_OUT_FROM_TOP, 2800},     // Front speaks by leaving (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 400},                                 // Brief back-only moment
  {ANIMATE, FRONT_MASK, WIPE_IN_FROM_BOTTOM, 3000},   // Front returns (OFF→ON)
  {WAIT, 0, OFF, 600},                                 // Both lit
  {ANIMATE, BACK_MASK, WIPE_OUT_FROM_BOTTOM, 2800},   // Back responds by leaving (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 400},                                 // Brief front-only moment
  {ANIMATE, BACK_MASK, WIPE_IN_FROM_TOP, 3000},       // Back returns (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2000},                                // Both lit - contemplation
  
  // Individual expressions - balanced front/back (1:18-1:48)
  {ANIMATE, PART_1_MASK, BREATHE, 4500},              // Part 1 breathes alone (ON→ON)
  {WAIT, 0, OFF, 800},
  {ANIMATE, PART_4_MASK, BREATHE, 4500},              // Part 4 responds diagonally (ON→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},
  {ANIMATE, PART_2_MASK, PULSE, 3500},                // Part 2 pulses (ON→ON)
  {WAIT, 0, OFF, 600},
  {ANIMATE, PART_3_MASK, PULSE, 3500},                // Part 3 pulses back (ON→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2000},                                // All lit - building intensity
  
  // Wave pattern - cascading wipes (1:48-2:03)
  {ANIMATE, PART_1_MASK, WIPE_OUT_FROM_TOP, 1800},    // Wave starts (ON→OFF)
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_2_MASK, WIPE_OUT_FROM_TOP, 1800},    // Wave continues (ON→OFF)
  {WAIT, 0, OFF, 400},                                 // Brief front-dark moment
  {ANIMATE, PART_3_MASK, WIPE_OUT_FROM_BOTTOM, 1800}, // Wave to back (ON→OFF)
  {WAIT, 0, OFF, 200},
  {ANIMATE, PART_4_MASK, WIPE_OUT_FROM_BOTTOM, 1800}, // Wave completes (ON→OFF)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},                                // Brief all-dark pause
  
  // Final emergence - simultaneous awakening (2:03-2:11)
  {ANIMATE, FRONT_MASK, WIPE_IN_FROM_BOTTOM, 4000},   // Front rises (OFF→ON)
  {ANIMATE, BACK_MASK, WIPE_IN_FROM_TOP, 4000},       // Back descends (OFF→ON)
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 4000},                                // Final harmony - both lit
  {ANIMATE, FRONT_MASK, FADE_OUT, 2500},              // Gentle fade to end
  {ANIMATE, BACK_MASK, FADE_OUT, 2500},               // Together into silence
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 56600}                                // Final silence to reach exactly 131000ms total
};

// Friend composition - intimate, acoustic feeling  
const Composition friendCompositionSaman = {
  "Friend - Saman",
  friendCommands,
  sizeof(friendCommands)/sizeof(Command),
  false,        // Play once
  131000        // Precise duration: 2:11 = 131 seconds
};

// Barry White composition - romantic, fragile, and tender (4:51 duration)
// Inspired by "Just the Way You Are" - smooth, emotional, with gentle vulnerability
const Command barryWhiteCommands[] = {
  // Romantic opening - gentle awakening (0:00-0:45)
  {ANIMATE, PART_1_MASK, BREATHE, 5000},     // Tender start
  {WAIT, 0, OFF, 800},                       // Gentle pause
  {ANIMATE, PART_3_MASK, BREATHE, 5500},     // Back responds lovingly
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1200},                      // Contemplative silence
  
  // Fragile connection - tentative touching (0:45-1:30)
  {ANIMATE, PART_2_MASK, FADE_IN, 3000},     // Slow, vulnerable approach
  {WAIT, 0, OFF, 1000},
  {ANIMATE, PART_4_MASK, FADE_IN, 3200},     // Hesitant but warm response
  {WAIT, 0, OFF, 1500},
  {ANIMATE, FRONT_MASK, FADE_OUT, 4000},     // Gentle retreat
  {ANIMATE, BACK_MASK, FADE_OUT, 4000},      // Synchronized withdrawal
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 2000},                      // Longing pause
  
  // Tender confession - building intimacy (1:30-2:30)
  {ANIMATE, FRONT_MASK, BREATHE, 6000},      // Front breathes together
  {WAIT, 0, OFF, 800},
  {ANIMATE, BACK_MASK, BREATHE, 6500},       // Back joins in harmony
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},
  {ANIMATE, PART_1_MASK, PULSE, 4000},       // Individual expression
  {WAIT, 0, OFF, 600},
  {ANIMATE, PART_4_MASK, PULSE, 4000},       // Diagonal response
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 800},
  {ANIMATE, PART_2_MASK, PULSE, 4000},       // Continuing dialogue
  {WAIT, 0, OFF, 600},
  {ANIMATE, PART_3_MASK, PULSE, 4000},       // Completing the conversation
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1500},
  
  // Emotional peak - fragile vulnerability (2:30-3:45)
  {ANIMATE, FRONT_MASK, FADE_IN, 2500},      // Building intensity
  {WAIT, 0, OFF, 1000},
  {ANIMATE, FRONT_MASK, FADE_OUT, 3000},     // Emotional release
  {WAIT, 0, OFF, 800},
  {ANIMATE, BACK_MASK, FADE_IN, 2500},       // Supportive response
  {WAIT, 0, OFF, 1000},
  {ANIMATE, BACK_MASK, FADE_OUT, 3000},      // Gentle comfort
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1200},
  
  // Fragile dance - delicate interplay (3:45-4:30)
  {ANIMATE, PART_1_MASK, BREATHE, 3500},     // Delicate breathing
  {WAIT, 0, OFF, 400},
  {ANIMATE, PART_2_MASK, BREATHE, 3500},     // Synchronized breathing
  {WAIT, 0, OFF, 400},
  {ANIMATE, PART_3_MASK, BREATHE, 3500},     // Back joins gently
  {WAIT, 0, OFF, 400},
  {ANIMATE, PART_4_MASK, BREATHE, 3500},     // All breathing together
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 1000},
  
  // Tender resolution - loving acceptance (4:30-4:51)
  {ANIMATE, FRONT_MASK, FADE_IN, 4000},      // Gentle awakening
  {WAIT, 0, OFF, 1200},
  {ANIMATE, BACK_MASK, FADE_IN, 4000},       // Harmonious joining
  {WAIT, 0, OFF, 2000},
  {ANIMATE, FRONT_MASK, FADE_OUT, 8000},     // Slow, loving fade
  {ANIMATE, BACK_MASK, FADE_OUT, 8000},      // Together into silence
  {WAIT_COMPLETE, 0, OFF, 0},
  {WAIT, 0, OFF, 3000}                       // Final romantic silence
};

// Barry White composition - romantic and fragile
const Composition barryWhiteComposition = {
  "Barry White - Just the Way You Are",
  barryWhiteCommands,
  sizeof(barryWhiteCommands)/sizeof(Command),
  false,        // Play once
  291000        // Precise duration: 4:51 = 291 seconds
};

// Debug animation - flashes first and last LEDs of each part for identification

const Command debugCommands[] = {
  // Flash first and last LEDs bright
  {DEBUG_FLASH, 0, OFF, 255},               // Full brightness flash
  {WAIT, 0, OFF, 300},                      // Hold for visibility
  {DEBUG_FLASH, 0, OFF, 0},                 // Turn off
  {WAIT, 0, OFF, 200},                      // Brief pause
  
  {DEBUG_FLASH, 0, OFF, 255},               // Full brightness flash
  {WAIT, 0, OFF, 300},                      // Hold for visibility  
  {DEBUG_FLASH, 0, OFF, 0},                 // Turn off
  {WAIT, 0, OFF, 200},                      // Brief pause
  
  {DEBUG_FLASH, 0, OFF, 255},               // Full brightness flash
  {WAIT, 0, OFF, 300},                      // Hold for visibility
  {DEBUG_FLASH, 0, OFF, 0},                 // Turn off
  {WAIT, 0, OFF, 500}                       // Longer pause after debug
};

const Composition debugComposition = {
  "Debug - First/Last LEDs",
  debugCommands,
  sizeof(debugCommands)/sizeof(Command),
  false,
  0
};

// === EASING FUNCTIONS ===

/*
 * Easing functions provide natural motion curves instead of linear progression
 * All functions take a progress value from 0.0 to 1.0 and return an eased value
 */

// Ease in (slow start, accelerating)
float easeIn(float t) {
  return t * t * t;  // Cubic ease-in
}

// Ease out (fast start, decelerating) 
float easeOut(float t) {
  float f = t - 1.0;
  return f * f * f + 1.0;  // Cubic ease-out
}

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
        part->currentBrightness = 0;
        // Set all LEDs to off
        for (int i = part->startLED; i <= part->endLED; i++) {
          part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, 0));
        }
        break;
        
      case FADE_IN:
      case WIPE_IN_FROM_TOP:
      case WIPE_IN_FROM_BOTTOM:
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
      // Wipe animations are handled LED by LED, brightness set to full
      brightness = 255;
      break;
      
    case OFF:
      brightness = 0;
      break;
  }
  
  part->currentBrightness = brightness;
  
  // Handle wipe animations LED by LED, others apply to all LEDs
  if (part->animation.type == WIPE_IN_FROM_TOP || 
      part->animation.type == WIPE_IN_FROM_BOTTOM ||
      part->animation.type == WIPE_OUT_FROM_TOP || 
      part->animation.type == WIPE_OUT_FROM_BOTTOM) {
    
    int totalLEDs = part->endLED - part->startLED + 1;
    
    // Apply symmetrical easing to all wipe animations for consistent, natural movement
    float easedProgress = easeInOut(progress);  // Symmetrical: slow start, fast middle, slow end
    
    // Calculate position with sub-LED precision for smooth fading
    float exactPosition = totalLEDs * easedProgress;
    int activeLEDs = (int)exactPosition;
    float fadeAmount = exactPosition - activeLEDs;  // Fractional part for fade
    
    // Clear all LEDs first
    for (int i = part->startLED; i <= part->endLED; i++) {
      part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, 0));
    }
    
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
  
  // Start with all parts off
  clearAllParts();
}

// === MAIN LOOP ===
void loop()
{
  // Debug pattern - flash first and last LEDs to identify part boundaries
  executeComposition(debugComposition);
  
  // Main composition
  executeComposition(friendCompositionSaman);
  
  // Debug pattern - flash first and last LEDs after composition
  executeComposition(debugComposition);
  
  // Longer pause between repetitions
  delay(8000);
}
