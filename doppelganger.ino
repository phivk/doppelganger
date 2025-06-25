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
 * 
 * PART LAYOUT:
 * The 120 total LEDs are divided into 4 logical parts:
 * - Part 0: Strip1, LEDs 0-29  (first half of strip 1)
 * - Part 1: Strip1, LEDs 30-59 (second half of strip 1)  
 * - Part 2: Strip2, LEDs 0-29  (first half of strip 2)
 * - Part 3: Strip2, LEDs 30-59 (second half of strip 2)
 * 
 * ==============================================================================
 */

#include <Adafruit_NeoPixel.h>

// === HARDWARE CONFIGURATION ===
#define PIN1 2              // Strip 1 data pin
#define PIN2 3              // Strip 2 data pin  
#define NUM_LEDS 60         // LEDs per strip
#define BRIGHTNESS 50       // Global brightness (0-255)

Adafruit_NeoPixel strip1(NUM_LEDS, PIN1, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, PIN2, NEO_GRBW + NEO_KHZ800);

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
// This creates a 2x2 grid where each quadrant can be controlled independently
LEDPart parts[] = {
  {&strip1, 0, 29, 0, {OFF, 0, 0, false}},   // Part 0: Strip1 first half
  {&strip1, 30, 59, 0, {OFF, 0, 0, false}},  // Part 1: Strip1 second half
  {&strip2, 0, 29, 0, {OFF, 0, 0, false}},   // Part 2: Strip2 first half
  {&strip2, 30, 59, 0, {OFF, 0, 0, false}}   // Part 3: Strip2 second half
};

#define NUM_PARTS 4

// === ANIMATION FRAMEWORK PROTOTYPES ===
void startAnimation(int partIndex, AnimationType type, uint16_t duration);
void startAnimationOnParts(int* partIndices, int count, AnimationType type, uint16_t duration);
void startSequentialAnimation(int* partIndices, int count, AnimationType type, uint16_t duration, uint16_t delayBetween);
void updateAllAnimations();
void updatePartAnimation(int partIndex);
void clearAllParts();
bool isAnyPartActive();

// === HIGH-LEVEL COMPOSITION FUNCTIONS ===
void createWavePattern(uint16_t duration);
void createOppositePairs(uint16_t duration);
void createAllTogether(uint16_t duration);
void createBreathingSequence(uint16_t duration);
void createChasePattern(uint16_t duration);

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
  
  // Refresh both strips to show the updated colors
  strip1.show();
  strip2.show();
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
    if (part->animation.type == FADE_OUT || part->animation.type == OFF) {
      part->currentBrightness = 0;
    }
    // Set final brightness and update LEDs
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
      break;
      
    case PULSE:
      // Pulse: fade in for first half, fade out for second half
      if (progress <= 0.5) {
        brightness = (uint8_t)(255 * (progress * 2));
      } else {
        brightness = (uint8_t)(255 * (2 - progress * 2));
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
    for (int j = parts[i].startLED; j <= parts[i].endLED; j++) {
      parts[i].strip->setPixelColor(j, parts[i].strip->Color(0, 0, 0, 0));
    }
  }
  strip1.show();
  strip2.show();
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
  // Initialize both LED strips
  strip1.begin();
  strip1.setBrightness(BRIGHTNESS);
  strip1.show();

  strip2.begin();
  strip2.setBrightness(BRIGHTNESS);
  strip2.show();
  
  // Start with all parts off
  clearAllParts();
}

// === MAIN LOOP ===
void loop()
{
  // Demonstrate different animation patterns
  // Each pattern shows different ways to compose part animations
  
  createWavePattern(2000);    // Wave flowing across all parts
  delay(1000);
  
  createOppositePairs(2000);  // Opposite corners light up
  delay(1000);
  
  createAllTogether(3000);    // All parts pulse in unison
  delay(1000);
  
  createBreathingSequence(4000); // Strips breathe alternately
  delay(1000);
  
  createChasePattern(3000);   // Parts light up in sequence
  delay(2000);
}

// === HIGH-LEVEL COMPOSITION FUNCTIONS ===
// These demonstrate different patterns you can create by combining
// the basic animation functions in various ways.

/*
 * WAVE PATTERN
 * Creates a wave effect that flows from part 0 to part 3,
 * then reverses direction. This demonstrates sequential animations.
 * 
 * Pattern: 0 → 1 → 2 → 3 (fade in), then 3 → 2 → 1 → 0 (fade out)
 */
void createWavePattern(uint16_t duration) {
  clearAllParts();
  
  // Wave from Part 0 to Part 3
  int sequence[] = {0, 1, 2, 3};
  startSequentialAnimation(sequence, 4, FADE_IN, duration, duration / 4);
  
  // Wait for all to finish, then fade out in reverse
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  int reverseSequence[] = {3, 2, 1, 0};
  startSequentialAnimation(reverseSequence, 4, FADE_OUT, duration, duration / 4);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * OPPOSITE PAIRS PATTERN  
 * Lights up diagonal opposite parts in alternation.
 * This demonstrates simultaneous animations on multiple parts.
 * 
 * Pattern: Parts 0&3 pulse together, then parts 1&2 pulse together
 * 
 * Visual layout:
 *   [0] [1]
 *   [2] [3]
 * First: 0&3 (diagonal), then 1&2 (other diagonal)
 */
void createOppositePairs(uint16_t duration) {
  clearAllParts();
  
  // First pair: Parts 0 and 3 (opposite corners)
  int pair1[] = {0, 3};
  startAnimationOnParts(pair1, 2, PULSE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(500);
  
  // Second pair: Parts 1 and 2 (other opposite corners)
  int pair2[] = {1, 2};
  startAnimationOnParts(pair2, 2, PULSE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * ALL TOGETHER PATTERN
 * All parts pulse simultaneously, creating a unified effect.
 * This demonstrates coordinated animation across all parts.
 */
void createAllTogether(uint16_t duration) {
  clearAllParts();
  
  // All parts pulse together
  int allParts[] = {0, 1, 2, 3};
  startAnimationOnParts(allParts, 4, PULSE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * BREATHING SEQUENCE PATTERN
 * Each strip breathes in turn, creating an alternating rhythm.
 * This demonstrates strip-level coordination and the BREATHE animation type.
 * 
 * Pattern: Strip 1 (parts 0&1) breathes, then Strip 2 (parts 2&3) breathes
 */
void createBreathingSequence(uint16_t duration) {
  clearAllParts();
  
  // Strip 1 breathes first (parts 0 and 1)
  int strip1Parts[] = {0, 1};
  startAnimationOnParts(strip1Parts, 2, BREATHE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
  
  delay(500);
  
  // Strip 2 breathes second (parts 2 and 3)
  int strip2Parts[] = {2, 3};
  startAnimationOnParts(strip2Parts, 2, BREATHE, duration);
  
  while (isAnyPartActive()) {
    updateAllAnimations();
    delay(10);
  }
}

/*
 * CHASE PATTERN
 * Creates a chase effect where each part lights up briefly in sequence
 * with overlapping fade in/out. This demonstrates complex timing coordination.
 * 
 * Each part fades in, then starts fading out while the next part starts fading in,
 * creating a smooth chase effect across all parts.
 */
void createChasePattern(uint16_t duration) {
  clearAllParts();
  
  // Chase pattern: each part lights up briefly in sequence
  for (int i = 0; i < NUM_PARTS; i++) {
    startAnimation(i, FADE_IN, duration / 4);
    
    // Wait a bit, then start fading out while next one starts
    delay(duration / 8);
    startAnimation(i, FADE_OUT, duration / 4);
    
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

