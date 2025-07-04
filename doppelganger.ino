/*
 * ==============================================================================
 * DOPPELGÄNGER - Part-Based LED Animation Framework
 * ==============================================================================
 * 
 * This Arduino sketch implements a flexible animation framework for controlling
 * multiple LED strips divided into logical "parts" that can be animated 
 * independently or in coordination.
 * 
 * HARDWARE SETUP:
 * - Strip 1: Connected to pin 2, 60 LEDs (GRBW NeoPixels)
 * - Strip 2: Connected to pin 3, 60 LEDs (GRBW NeoPixels)
 * - Strip 3: Connected to pin 4, 60 LEDs (GRBW NeoPixels)
 * - Strip 4: Connected to pin 5, 60 LEDs (GRBW NeoPixels)
 * 
 * PART LAYOUT:
 * The system uses 4 logical parts arranged in a frame:
 * - Part 0: Strip1, LEDs 40-59 (Front A-Side)
 * - Part 1: Strip2, LEDs 40-59 (Front B-Side)
 * - Part 2: Strip3, LEDs 40-59 (Back A-Side)
 * - Part 3: Strip4, LEDs 40-59 (Back B-Side)
 * 
 * Physical Layout:
 *   Front: [Part 0] [Part 1]
 *   Back:  [Part 2] [Part 3]
 *          A Side   B Side
 * 
 * CONSTRAINTS:
 * - Only FRONT or BACK can be lit at any time, never both
 * - Front parts (0 & 1) can be on together
 * - Back parts (2 & 3) can be on together
 * - Any front + back combination is forbidden
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
// Each part uses the full 40-59 LED range on each strip
LEDPart parts[] = {
  {&strip1, 40, 59, 0, {OFF, 0, 0, false}},   // Part 0: Strip1 LEDs 40-59
  {&strip2, 40, 59, 0, {OFF, 0, 0, false}},   // Part 1: Strip2 LEDs 40-59
  {&strip3, 40, 59, 0, {OFF, 0, 0, false}},   // Part 2: Strip3 LEDs 40-59
  {&strip4, 40, 59, 0, {OFF, 0, 0, false}}    // Part 3: Strip4 LEDs 40-59
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

// === ANIMATION FRAMEWORK PROTOTYPES ===
void startAnimation(int partIndex, AnimationType type, uint16_t duration);
void startAnimationOnParts(int* partIndices, int count, AnimationType type, uint16_t duration);
void startSequentialAnimation(int* partIndices, int count, AnimationType type, uint16_t duration, uint16_t delayBetween);
void updateAllAnimations();
void updatePartAnimation(int partIndex);
void clearAllParts();
bool isAnyPartActive();

// === HIGH-LEVEL COMPOSITION FUNCTIONS ===
void createWavePattern(uint16_t baseDuration);
void createOppositePairs(uint16_t baseDuration);
void createAllTogether(uint16_t baseDuration);
void createBreathingSequence(uint16_t baseDuration);
void createChasePattern(uint16_t baseDuration);
void createDoppelgangerPattern(uint16_t baseDuration);

// === ANIMATION FRAMEWORK IMPLEMENTATION ===

/*
 * Start an animation on a single part
 * 
 * partIndex: Which part to animate (0-3)
 * type: What kind of animation to run
 * duration: How long the animation should take in milliseconds
 * 
 * This is the fundamental building block - all other animation functions
 * ultimately call this to start individual part animations.
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
 * Start the same animation on multiple parts simultaneously
 * 
 * Example usage:
 *   int corners[] = {0, 3}; // Opposite corners
 *   startAnimationOnParts(corners, 2, PULSE, 2000);
 * 
 * This creates synchronized effects across multiple parts.
 */
void startAnimationOnParts(int* partIndices, int count, AnimationType type, uint16_t duration) {
  for (int i = 0; i < count; i++) {
    startAnimation(partIndices[i], type, duration);
  }
}

/*
 * Start animations in sequence with delays between each
 * 
 * Example usage:
 *   int wave[] = {0, 1, 2, 3}; // All parts in order
 *   startSequentialAnimation(wave, 4, FADE_IN, 2000, 500);
 * 
 * This creates wave-like effects that flow across the parts.
 * Each animation starts 'delayBetween' milliseconds after the previous one.
 */
void startSequentialAnimation(int* partIndices, int count, AnimationType type, uint16_t duration, uint16_t delayBetween) {
  for (int i = 0; i < count; i++) {
    startAnimation(partIndices[i], type, duration);
    if (i < count - 1) { // Don't delay after the last animation
      delay(delayBetween);
    }
  }
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
  // Demonstrate different animation patterns that respect the physical constraints
  // Each pattern ensures A-side parts (0&2) and B-side parts (1&3) are never on simultaneously
  // Now with dynamic tempo that accelerates and decelerates over time!
  
  createWavePattern(800);         // Constraint-safe wave: 0→3→1→2
  delay(getDelayDuration(300));
  
  createOppositePairs(600);       // Only diagonal pairs: 0&3, then 1&2
  delay(getDelayDuration(300));
  
  createAllTogether(1000);        // Rapid alternating diagonals
  delay(getDelayDuration(300));
  
  createBreathingSequence(1200);  // Sequential breathing: 0→3→1→2
  delay(getDelayDuration(300));
  
  createChasePattern(1000);       // Constraint-safe chase
  delay(getDelayDuration(300));
  
  createDoppelgangerPattern(800); // Special doppelganger effect
  delay(getDelayDuration(500));
}

// === HIGH-LEVEL COMPOSITION FUNCTIONS ===
// These demonstrate different patterns you can create by combining
// the basic animation functions in various ways.

/*
 * WAVE PATTERN
 * Creates a wave effect that alternates between front and back.
 * Wave flows across the front, then across the back.
 * 
 * Pattern: Front wave (0 → 1), then Back wave (2 → 3)
 */
void createWavePattern(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  
  // Front wave: 0 → 1
  int frontSequence[] = {0, 1};
  startSequentialAnimation(frontSequence, 2, FADE_IN, duration / 2, duration / 4);
  
  // Wait for front wave to complete
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  // Fade out front
  startSequentialAnimation(frontSequence, 2, FADE_OUT, duration / 2, duration / 4);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(getDelayDuration(200));
  
  // Back wave: 2 → 3
  int backSequence[] = {2, 3};
  startSequentialAnimation(backSequence, 2, FADE_IN, duration / 2, duration / 4);
  
  // Wait for back wave to complete
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  // Fade out back
  startSequentialAnimation(backSequence, 2, FADE_OUT, duration / 2, duration / 4);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * OPPOSITE PAIRS PATTERN  
 * Alternates between front pairs and back pairs.
 * Shows the "doppelganger" effect by switching between front and back views.
 * 
 * Pattern: Front (0&1) together, then Back (2&3) together
 */
void createOppositePairs(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  uint16_t delayTime = getDelayDuration(500);
  
  // Front pair: Parts 0 and 1 together
  int frontPair[] = {0, 1};
  startAnimationOnParts(frontPair, 2, PULSE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Back pair: Parts 2 and 3 together
  int backPair[] = {2, 3};
  startAnimationOnParts(backPair, 2, PULSE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * ALL TOGETHER PATTERN
 * Since we can't have front and back on simultaneously,
 * this alternates rapidly between "all front" and "all back".
 * 
 * Pattern: Front (0&1) and Back (2&3) alternate rapidly
 */
void createAllTogether(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  
  // Alternate between front and back rapidly
  uint32_t startTime = millis();
  uint16_t switchInterval = duration / 10; // Switch every 1/10th of duration
  
  while (millis() - startTime < duration) {
    // Front parts: 0 & 1
    int frontParts[] = {0, 1};
    startAnimationOnParts(frontParts, 2, FADE_IN, switchInterval);
    
    // Wait for switch interval
    uint32_t phaseStart = millis();
    while (millis() - phaseStart < switchInterval) {
      updateAllAnimations();
      delay(10);
    }
    
    // Clear and switch to back parts: 2 & 3
    clearAllParts();
    int backParts[] = {2, 3};
    startAnimationOnParts(backParts, 2, FADE_IN, switchInterval);
    
    // Wait for switch interval
    phaseStart = millis();
    while (millis() - phaseStart < switchInterval) {
      updateAllAnimations();
      delay(10);
    }
    
    clearAllParts();
  }
}

/*
 * BREATHING SEQUENCE PATTERN
 * Each part breathes individually, alternating between front and back.
 * 
 * Pattern: Front A (0) → Front B (1) → Back A (2) → Back B (3)
 */
void createBreathingSequence(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  uint16_t delayTime = getDelayDuration(500);
  
  // Breathing sequence: 0 → 1 → 2 → 3
  int sequence[] = {0, 1, 2, 3};
  
  for (int i = 0; i < 4; i++) {
    startAnimation(sequence[i], BREATHE, duration);
    
    while (isAnyPartActive()) {
      updateAllAnimations();
      delay(10);
    }
    
    if (i < 3) { // Don't delay after the last part
      delay(delayTime);
    }
  }
}

/*
 * CHASE PATTERN
 * Creates a chase effect that moves across front, then back.
 * Each part lights up briefly in sequence.
 * 
 * Pattern: 0 → 1 → 2 → 3 with overlapping fades
 */
void createChasePattern(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  
  // Chase pattern: 0 → 1 → 2 → 3
  int sequence[] = {0, 1, 2, 3};
  
  for (int i = 0; i < 4; i++) {
    startAnimation(sequence[i], FADE_IN, duration / 4);
    
    // Wait a bit, then start fading out while next one starts
    delay(duration / 8);
    startAnimation(sequence[i], FADE_OUT, duration / 4);
    
    // Keep updating animations during the chase
    for (int j = 0; j < 20; j++) {
      updateAllAnimations();
      delay(duration / 160); // Small delay for smooth animation
    }
  }
  
  // Wait for all animations to complete
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * DOPPELGANGER SPECIAL PATTERN
 * Showcases the front/back alternation effect.
 * Shows front view, then back view, creating a "flip" effect.
 * 
 * Pattern: All Front → All Back → Individual Front → Individual Back
 */
void createDoppelgangerPattern(uint16_t baseDuration) {
  clearAllParts();
  
  uint16_t duration = getAnimationDuration(baseDuration);
  uint16_t delayTime = getDelayDuration(200);
  
  // Show all front (Parts 0 & 1)
  int frontParts[] = {0, 1};
  startAnimationOnParts(frontParts, 2, PULSE, duration / 2);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Show all back (Parts 2 & 3) - the doppelganger view
  int backParts[] = {2, 3};
  startAnimationOnParts(backParts, 2, PULSE, duration / 2);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Individual front A (Part 0)
  startAnimation(0, PULSE, duration / 3);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Individual back A (Part 2) - doppelganger of Part 0
  startAnimation(2, PULSE, duration / 3);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Individual front B (Part 1)
  startAnimation(1, PULSE, duration / 3);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(delayTime);
  
  // Individual back B (Part 3) - doppelganger of Part 1
  startAnimation(3, PULSE, duration / 3);
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

