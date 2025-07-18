# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**DOPPELGÄNGER** is an interactive art installation for Borderland 2025. Two participants sit across from each other separated by a semi-transparent mirror. The lighting system alternates between sides, creating an effect where participants see themselves, each other, or a fusion of both faces.

## Hardware Architecture

### LED Configuration
- **4 NeoPixel LED strips** (GRBW, 60 LEDs each)
  - Strip 1: Pin 2 (Part 0 - Front A-Side, LEDs 40-59)
  - Strip 2: Pin 3 (Part 1 - Front B-Side, LEDs 40-59)  
  - Strip 3: Pin 4 (Part 2 - Back A-Side, LEDs 40-59)
  - Strip 4: Pin 5 (Part 3 - Back B-Side, LEDs 40-59)

### Physical Layout
```
Front: [Part 0] [Part 1]
Back:  [Part 2] [Part 3]
       A Side   B Side
```

### Critical Hardware Constraints
- **NEVER light front and back simultaneously** - this breaks the mirror effect
- Front parts (0 & 1) can be on together
- Back parts (2 & 3) can be on together
- Any front + back combination is forbidden

## Software Architecture

### Animation Framework
The code uses a **part-based animation system** where each "part" is a logical section of LEDs that can be animated independently:

- **AnimationType**: OFF, FADE_IN, FADE_OUT, PULSE, BREATHE
- **LEDPart**: Defines strip reference, LED range, brightness, and animation state
- **AnimationState**: Tracks animation progress and timing

### Core Animation Functions
- `startAnimation(partIndex, type, duration)` - Start animation on single part
- `startAnimationOnParts(parts[], count, type, duration)` - Synchronize multiple parts
- `startSequentialAnimation(parts[], count, type, duration, delay)` - Sequential with delays
- `updateAllAnimations()` - Main animation loop processor

### Dynamic Tempo System
The installation features a **20-second tempo cycle**:
- Starts slow (0.3x speed)
- Accelerates exponentially to peak (3.0x speed) at 70% of cycle
- Quick deceleration back to slow (30% of cycle)
- All durations and delays are tempo-adjusted via `getAnimationDuration()` and `getDelayDuration()`

### High-Level Pattern Functions
- `createWavePattern()` - Wave across front, then back
- `createOppositePairs()` - Alternates front pairs and back pairs
- `createDoppelgangerPattern()` - Showcases front/back alternation effect
- `createBreathingSequence()` - Individual breathing sequence
- `createChasePattern()` - Chase with overlapping fades
- `createAllTogether()` - Rapid front/back alternation

## Development Commands

### Arduino IDE
- **Upload**: Use Arduino IDE with board set to your microcontroller
- **Monitor**: Serial Monitor at 9600 baud (no serial output currently implemented)
- **Libraries**: Requires Adafruit NeoPixel library

### Testing Hardware
- Test individual strips by modifying `setup()` to light specific parts
- Use `clearAllParts()` to reset all LEDs
- Monitor animations with `isAnyPartActive()` in test loops

## Key Implementation Notes

### LED Control
- Uses **white channel only** for GRBW strips: `strip->setPixelColor(i, strip->Color(0, 0, 0, brightness))`
- Global brightness set to 50/255 in hardware configuration
- All LEDs explicitly set to ensure clean state transitions

### Animation Timing
- Main loop should call `updateAllAnimations()` every 10ms when animations are active
- Tempo system smoothly interpolates speed changes to avoid jarring transitions
- Sequential animations use tempo-adjusted delays between parts

### Constraint Enforcement
All high-level patterns are designed to respect the front/back constraint. When creating new patterns, always ensure:
1. Check which parts are front (0,1) vs back (2,3)
2. Never activate front and back parts simultaneously
3. Use `clearAllParts()` between conflicting transitions

## File Structure
```
doppelganger/
├── doppelganger.ino          # Main Arduino sketch
├── README.md                 # Project description and context
├── wiring_diagram.svg        # Hardware wiring reference
└── CLAUDE.md                 # This file
```