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
 * - Part 1: Strip1, LEDs 40-59 (Front A-Side) - Bitmask: 0b0001
 * - Part 2: Strip2, LEDs 40-59 (Front B-Side) - Bitmask: 0b0010
 * - Part 3: Strip3, LEDs 40-59 (Back A-Side)  - Bitmask: 0b0100
 * - Part 4: Strip4, LEDs 40-59 (Back B-Side)  - Bitmask: 0b1000
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
  OFF,        // Immediate off
  FADE_IN,    // 0 → full brightness over duration
  FADE_OUT,   // full brightness → 0 over duration  
  PULSE,      // fade in then fade out (complete cycle)
  BREATHE     // slow inhale (70%), quick exhale (30%)
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
  {&strip1, 40, 59, 0, {OFF, 0, 0, false}},   // parts[0] = Part 1: Strip1 LEDs 40-59
  {&strip2, 40, 59, 0, {OFF, 0, 0, false}},   // parts[1] = Part 2: Strip2 LEDs 40-59
  {&strip3, 40, 59, 0, {OFF, 0, 0, false}},   // parts[2] = Part 3: Strip3 LEDs 40-59
  {&strip4, 40, 59, 0, {OFF, 0, 0, false}}    // parts[3] = Part 4: Strip4 LEDs 40-59
};

#define NUM_PARTS 4

// === DYNAMIC TEMPO SYSTEM ===
struct TempoState {
  float currentSpeed;        // Current speed multiplier (1.0 = normal, 0.5 = half speed, 2.0 = double speed)
  float targetSpeed;         // Target speed we're moving towards
  uint32_t lastUpdateTime;   // Last time we updated the tempo
  int phase;                 // 0 = accelerating, 1 = decelerating
  uint32_t cycleStartTime;   // When the current cycle started
  uint16_t cycleDuration;    // How long each complete cycle should take
};

TempoState tempo = {0.3, 0.3, 0, 0, 0, 20000}; // Start slow (0.3x speed), 20 second cycles

// Calculate current tempo based on acceleration curve
void updateTempo() {
  uint32_t currentTime = millis();
  uint32_t elapsed = currentTime - tempo.cycleStartTime;
  
  // If we've completed a cycle, start a new one
  if (elapsed >= tempo.cycleDuration) {
    tempo.cycleStartTime = currentTime;
    elapsed = 0;
  }
  
  // Calculate progress through the cycle (0.0 to 1.0)
  float cycleProgress = (float)elapsed / (float)tempo.cycleDuration;
  
  // Define the speed curve: slow start, accelerate to peak at 70%, then quick return
  float targetSpeed;
  if (cycleProgress <= 0.7) {
    // Acceleration phase: 70% of cycle time
    // Use exponential curve for smooth acceleration
    float accelerationProgress = cycleProgress / 0.7;
    targetSpeed = 0.3 + (3.0 - 0.3) * (accelerationProgress * accelerationProgress);
  } else {
    // Deceleration phase: 30% of cycle time (quick but smooth return)
    float decelerationProgress = (cycleProgress - 0.7) / 0.3;
    // Use inverse exponential for quick but smooth deceleration
    targetSpeed = 3.0 - (3.0 - 0.3) * (decelerationProgress * decelerationProgress);
  }
  
  // Smooth the speed changes to avoid jarring transitions
  float speedDiff = targetSpeed - tempo.currentSpeed;
  tempo.currentSpeed += speedDiff * 0.1; // Smooth interpolation
  
  tempo.lastUpdateTime = currentTime;
}

// Get current animation duration based on tempo
uint16_t getAnimationDuration(uint16_t baseDuration) {
  updateTempo();
  return (uint16_t)(baseDuration / tempo.currentSpeed);
}

// Get current delay duration based on tempo
uint16_t getDelayDuration(uint16_t baseDelay) {
  return (uint16_t)(baseDelay / tempo.currentSpeed);
}

// === DECLARATIVE COMPOSITION SYSTEM ===

// Command types for declarative animation sequences
enum CommandType {
  ANIMATE,      // Start animation on specified parts
  WAIT,         // Wait for specified duration
  WAIT_COMPLETE // Wait for all active animations to complete
};

// Animation command structure
struct Command {
  CommandType cmd;
  uint8_t partMask;     // Bitmask: bit 0=Part1, bit 1=Part2, bit 2=Part3, bit 3=Part4
  AnimationType type;
  uint16_t duration;
};

// Animation definition
struct Animation {
  const char* name;
  const Command* commands;
  uint8_t commandCount;
  bool looping;
};

// Composition definition - a sequence of animations
struct Composition {
  const char* name;
  const Animation* animations;
  uint8_t animationCount;
  bool looping;
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

// === ANIMATION FRAMEWORK PROTOTYPES ===
void startAnimation(int partIndex, AnimationType type, uint16_t duration);
void updateAllAnimations();
void updatePartAnimation(int partIndex);
void clearAllParts();
bool isAnyPartActive();

// === DECLARATIVE ANIMATION FUNCTIONS ===
bool isValidPartMask(uint8_t partMask);
void executeCommand(const Command& cmd);
void executeAnimation(const Animation& anim);
void executeComposition(const Composition& comp);
void animatePartsFromMask(uint8_t partMask, AnimationType type, uint16_t duration);


// === DECLARATIVE ANIMATION IMPLEMENTATIONS ===

/*
 * Validate that a part mask doesn't violate the front/back constraint
 * Returns true if the mask is valid (never front and back simultaneously)
 */
bool isValidPartMask(uint8_t partMask) {
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
      animatePartsFromMask(cmd.partMask, cmd.type, getAnimationDuration(cmd.duration));
      break;
      
    case WAIT:
      delay(getDelayDuration(cmd.duration));
      break;
      
    case WAIT_COMPLETE:
      while (isAnyPartActive()) {
        updateAllAnimations();
        delay(10);
      }
      break;
  }
}

/*
 * Execute a single animation
 */
void executeAnimation(const Animation& anim) {
  clearAllParts();
  
  do {
    for (int i = 0; i < anim.commandCount; i++) {
      executeCommand(anim.commands[i]);
      
      // Update animations during execution
      while (isAnyPartActive()) {
        updateAllAnimations();
        delay(10);
      }
    }
  } while (anim.looping);
}

/*
 * Execute a complete composition (sequence of animations)
 */
void executeComposition(const Composition& comp) {
  do {
    for (int i = 0; i < comp.animationCount; i++) {
      executeAnimation(comp.animations[i]);
      delay(getDelayDuration(300));  // Tempo-adjusted pause between animations
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

// Animation definitions
const Animation animations[] = {
  {"Wave", waveCommands, sizeof(waveCommands)/sizeof(Command), false},
  {"Opposite Pairs", oppositePairsCommands, sizeof(oppositePairsCommands)/sizeof(Command), false},
  {"All Together", allTogetherCommands, sizeof(allTogetherCommands)/sizeof(Command), false},
  {"Breathing Sequence", breathingSequenceCommands, sizeof(breathingSequenceCommands)/sizeof(Command), false},
  {"Chase Pattern", chasePatternCommands, sizeof(chasePatternCommands)/sizeof(Command), false},
  {"Doppelganger", doppelgangerPatternCommands, sizeof(doppelgangerPatternCommands)/sizeof(Command), false}
};

#define NUM_ANIMATIONS (sizeof(animations)/sizeof(Animation))

// === COMPOSITION DEFINITIONS ===

// Demo composition that cycles through all available animations
const Composition demoComposition = {
  "Demo - All Animations",
  animations,
  NUM_ANIMATIONS,
  true  // Loop indefinitely
};

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
    
    // Ensure final state is correct
    if (part->animation.type == FADE_OUT || part->animation.type == OFF) {
      part->currentBrightness = 0;
    } else if (part->animation.type == FADE_IN) {
      part->currentBrightness = 255;
    } else {
      // For PULSE and BREATHE, end at 0
      part->currentBrightness = 0;
    }
    
    // Explicitly set all LEDs in this part to the final brightness
    for (int i = part->startLED; i <= part->endLED; i++) {
      part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, part->currentBrightness));
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
      
    case OFF:
      brightness = 0;
      break;
  }
  
  part->currentBrightness = brightness;
  
  // Update all LEDs in this part to the calculated brightness
  // Using white channel (4th parameter) for GRBW strips
  for (int i = part->startLED; i <= part->endLED; i++) {
    part->strip->setPixelColor(i, part->strip->Color(0, 0, 0, brightness));
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
  // Execute the demo composition which cycles through all 6 animations:
  // 1. Wave: Front wave (1→2), then back wave (3→4)
  // 2. Opposite Pairs: Front (1&2) together, then back (3&4) together  
  // 3. All Together: Rapid front/back alternation
  // 4. Breathing Sequence: Individual breathing 1→2→3→4
  // 5. Chase Pattern: Overlapping fades 1→2→3→4
  // 6. Doppelganger: Complex front/back alternation pattern
  //
  // Each animation:
  // - Uses 1-indexed bitmask representation (Part 1 = 0b0001, etc.)
  // - Automatically respects front/back constraints
  // - Adapts to dynamic tempo system (20-second acceleration cycles)
  //
  // The composition loops indefinitely through all animations
  
  executeComposition(demoComposition);
}
