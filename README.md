# Doppelgänger

Two participants enter a secluding booth and sit across from each other. Separated by an unique mirror, one becomes the other.

Installation at Borderland 2025 by Camp Damiaan.

More info & budget: https://cobudget.com/borderland/borderland-dreams-2025/cm8xjg4sk45470cl9lp3u1oqu

## Full description

DOPPELGÄNGER is more than an installation—it's an experience of deep human connection. Two participants enter a secluding booth and sit across from each other, separated by an unique mirror. As the experience unfolds, the lighting shifts, and the mirror transforms - showing the others face, one's own or a fusion of both. At times, you’ll forget whose face you’re seeing, as your identities begin to blur and merge. It’s a place where the boundary between self and other becomes beautifully, and profoundly, uncertain.

Inside the booth, two sets of headphones provide a curated music experience, perfectly in sync with the rhythm of the lighting and the transformative visuals. The sound helps guide the participants through the journey, enhancing the connection and deepening the emotional experience.

The first time I experienced this, it left an unforgettable impact on me. I watched as my own reflection blurred into another’s, and in that moment, I saw myself in a way I never had before. I wasn’t alone—others around me emerged from the booth in awe, some laughing, some crying, all profoundly moved. That experience has stayed with me, and it’s what drives me to bring DOPPELGÄNGER to Borderland. I want others to feel what I felt—to step inside, to let go of the barriers between us, and to come out changed.

Strangers step into DOPPELGÄNGER with curiosity. It’s a space where barriers dissolve, where we see ourselves in others—sometimes quite literally—and where many have walked out in awe, in laughter, or even having fallen a little bit in love.

Technically the experience works by alternating light-levels on either side of a semi-transparent mirror. By rapidly alternating these levels showing (parts of) either side our sense of self starts to dissolve into the others face. The lights and sound will be pre-programmed and controlled by a microcontroller similar to an arduino.

## Technical Implementation

The DOPPELGÄNGER lighting system uses 4 LED strips controlled by an Arduino-compatible microcontroller. The system implements a sophisticated animation framework that respects the critical constraint of the semi-transparent mirror effect.

### Hardware Setup

- **4 GRBW NeoPixel LED strips** (60 LEDs each)
- **4 logical parts** mapped to specific LED segments:
  - Part 1: Front A-Side (Strip 1, LEDs 40-59)
  - Part 2: Front B-Side (Strip 2, LEDs 40-59)
  - Part 3: Back A-Side (Strip 3, LEDs 40-59)
  - Part 4: Back B-Side (Strip 4, LEDs 40-59)

### Critical Constraint

The system enforces a fundamental rule: **never illuminate front and back parts simultaneously**. This constraint is essential for the mirror effect to work properly - only the front view OR the back view can be lit at any time, never both.

### Animation Framework

The software implements a declarative composition system that makes it easy to create and modify lighting sequences:

**Animation Types:**

- FADE_IN: Gradual brightness increase
- FADE_OUT: Gradual brightness decrease
- PULSE: Complete fade in/out cycle
- BREATHE: Natural breathing rhythm (slow inhale, quick exhale)

**Command Structure:**

- ANIMATE: Start animation on specified parts
- WAIT: Pause for specified duration
- WAIT_COMPLETE: Wait for all active animations to finish

### Dynamic Tempo System

The installation features a 20-second tempo cycle that creates an evolving experience:

- Starts slow (0.3x speed)
- Accelerates to peak intensity (3.0x speed) at 70% of cycle
- Quick deceleration back to calm (30% of cycle)
- All animation durations automatically adjust to current tempo

### Built-in Compositions

The system includes 6 pre-programmed lighting compositions:

1. **Wave**: Flowing wave across front, then back
2. **Opposite Pairs**: Alternating between front and back pairs
3. **All Together**: Rapid front/back alternation
4. **Breathing Sequence**: Individual part breathing in sequence
5. **Chase Pattern**: Parts lighting in sequence with overlapping fades
6. **Doppelganger**: Complex pattern showcasing the mirror effect

### Extensibility

The declarative system is designed for easy extension:

- New compositions can be added by defining command arrays
- Bitmask representation makes part combinations intuitive
- Automatic constraint validation prevents invalid lighting states
- Ready for integration with user interaction triggers (buttons, sensors, etc.)
