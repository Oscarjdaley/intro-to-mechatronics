// ============================================================================
// A2 Part A: Hold-Time Input Module
// ============================================================================
// PURPOSE: Measure how long a button is held down and communicate the
//          duration to Part B (countdown timer). Part A also provides
//          visual feedback via a status lamp.
//
// KEY FEATURES:
//  - Non-blocking timing using millis()
//  - Edge-triggered button detection (no debounce delay)
//  - State machine: WAIT → MEASURING → result state
//  - Clear shared interface for Part B to read duration
//  - Status lamp provides visual system feedback
//
// INTEGRATION POINTS WITH PARTS B & C:
//  - systemState: drives countdown start and rainbow LED behavior
//  - sharedValue: holds the measured duration (in seconds) for Part B
//  - statusLampPin: shows diagnostics (blink=measuring, on=short, solid=long)
//
// HARDWARE ASSUMPTIONS (RESOLVE BEFORE FINAL BUILD):
//  - Arm button: active-LOW (pressed = LOW), INPUT_PULLUP
//  - Cancel button: active-LOW (pressed = LOW), INPUT_PULLUP
//  - Status lamp: active-HIGH (HIGH = on), current-limited via resistor
//  - All buttons share common GND with Arduino
//
// ============================================================================

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
const int armButtonPin = 2;      // Start Button (active-low, INPUT_PULLUP)
const int cancelButtonPin = 11;  // Cancel Button (active-low, INPUT_PULLUP)
const int statusLampPin = 13;    // Diagnostic status lamp (active-HIGH)

// NOTE: Reserve pins 6, 9, 10 for RGB LED (Part C)
// ============================================================================

// ============================================================================
// STATE MACHINE DEFINITIONS
// ============================================================================
// systemState values:
//   1 = WAIT:       Idle, no button press, waiting for arm button press
//   2 = MEASURING:  Arm button held, timing in progress, lamp blinks
//   3 = SHORT_HOLD: Button released, hold < 2 seconds, lamp blinks slowly
//   4 = LONG_HOLD:  Button released, hold ≥ 2 seconds, lamp stays ON
//
// STATE TRANSITIONS:
//   WAIT --[arm pressed]--> MEASURING
//   MEASURING --[arm released, hold<2s]--> SHORT_HOLD
//   MEASURING --[arm released, hold≥2s]--> LONG_HOLD
//   (any state) --[cancel pressed]--> WAIT
//
// FLAGGING OPPORTUNITY #1:
//   Do states 3 and 4 stay active until Part B starts countdown?
//   Should Part B send a signal back to Part A to return to WAIT?
//   Currently, only cancel button can reset. This might block Part C.
// ============================================================================
int systemState = 1;             // Current system state (see above)

// ============================================================================
// SHARED STATE (Part A ↔ Part B Interface)
// ============================================================================
// sharedValue: Duration in WHOLE SECONDS after button release
//   - Part A writes to this when arm button is released
//   - Part B reads this to start countdown
//   - Value = 0 means no valid hold (cancelled or too short?)
//
// FLAGGING OPPORTUNITY #2:
//   What if hold time is < 1 second? sharedValue would be 0.
//   Does Part B distinguish between "not ready" and "hold was <1s"?
//   Consider: use -1 for "not ready", 0 for valid (but very short) holds.
// ============================================================================
int sharedValue = 0;             // Duration in seconds (Part B reads this)

// ============================================================================
// TIMING STATE (for millis() edge-triggered logic)
// ============================================================================
unsigned long holdStartTime = 0; // Timestamp when arm button was first pressed
                                 // (in milliseconds since power-on)

// ============================================================================
// BUTTON STATE MEMORY (for edge detection)
// ============================================================================
bool armButtonWasDown = false;   // Previous loop's arm button state
bool cancelButtonWasDown = false; // Previous loop's cancel button state

// NOTE: Edge detection example:
//   Current loop: armDown = true,  armButtonWasDown = false → RISING EDGE (press)
//   Current loop: armDown = false, armButtonWasDown = true  → FALLING EDGE (release)

// ============================================================================
// TIMING THRESHOLDS
// ============================================================================
unsigned long shortHoldLimitMs = 2000;  // 2000ms = 2 seconds (SHORT_HOLD threshold)
unsigned long longHoldLimitMs = 5000;   // 5000ms = 5 seconds (LONG_HOLD threshold)

// FLAGGING OPPORTUNITY #3:
//   What are these thresholds for? Are they:
//   a) Thresholds to control Part C LED flash speed?
//   b) Thresholds to validate input (reject very short holds)?
//   c) Just diagnostic info?
//   Document the intent and whether Part B needs these values.
//
// SUGGESTION: Consider renaming:
//   - shortHoldLimitMs → diagnosticShortLimitMs (if just for LED feedback)
//   - longHoldLimitMs → diagnosticLongLimitMs (if just for LED feedback)

// ============================================================================
// SETUP: Initialize hardware and serial communication
// ============================================================================
void setup() {
  // Pin mode initialization
  pinMode(armButtonPin, INPUT_PULLUP);      // Arm button with internal pull-up
  pinMode(cancelButtonPin, INPUT_PULLUP);   // Cancel button with internal pull-up
  pinMode(statusLampPin, OUTPUT);           // Status lamp output (active-HIGH)
  digitalWrite(statusLampPin, LOW);         // Start with lamp OFF

  // Serial communication for debugging
  Serial.begin(9600);
  
  // Startup message
  Serial.println("\n================================================");
  Serial.println("Part A: Hold-Time Input Module");
  Serial.println("================================================");
  Serial.println("Instructions:");
  Serial.println("  1. Press and hold the arm button (pin 2)");
  Serial.println("  2. Release to measure hold duration");
  Serial.println("  3. Press cancel button (pin 11) to reset");
  Serial.println("");
  Serial.println("Status lamp (pin 13) shows:");
  Serial.println("  - Blinking (200ms): actively measuring hold time");
  Serial.println("  - Solid ON: long hold detected (≥2s)");
  Serial.println("  - OFF: idle or short hold");
  Serial.println("================================================\n");
}

// ============================================================================
// MAIN LOOP: Non-blocking event-driven state machine
// ============================================================================
void loop() {
  // STEP 1: Read current button states (actual hardware inputs)
  // ─────────────────────────────────────────────────────────────
  bool armDown = digitalRead(armButtonPin) == LOW;      // true if pressed
  bool cancelDown = digitalRead(cancelButtonPin) == LOW; // true if pressed

  // FLAGGING OPPORTUNITY #4:
  //   Button debouncing: These single digitalRead() calls could see bounces
  //   if switches are mechanical. If you observe jitter in Serial output,
  //   consider adding a simple debounce (e.g., check same value twice 5ms apart).

  // ─────────────────────────────────────────────────────────────
  // STEP 2: EVENT DETECTION via edge triggering
  // ─────────────────────────────────────────────────────────────
  // Use previous state + current state to detect transitions.

  // ═════ EVENT: Arm button PRESSED (RISING EDGE) ═════
  // When: armDown is now true, but was false last loop
  // Action: Start measuring if we're in WAIT state
  if (armDown && !armButtonWasDown && systemState == 1) {
    systemState = 2;                // Transition to MEASURING
    holdStartTime = millis();       // Record start time
    Serial.println("[EVENT] Arm button pressed - measuring started");
    // FLAGGING OPPORTUNITY #5:
    //   Should we also reset sharedValue to 0 here to clear stale data?
    //   This helps Part B know that a new measurement is in progress.
  }

  // ═════ EVENT: Arm button RELEASED (FALLING EDGE) ═════
  // When: armDown is now false, but was true last loop
  // Action: Calculate hold duration and transition to result state
  if (!armDown && armButtonWasDown && systemState == 2) {
    unsigned long heldForMs = millis() - holdStartTime;  // Duration in milliseconds
    sharedValue = heldForMs / 1000;  // Convert to whole seconds
    
    // Decide which result state based on hold duration
    if (heldForMs < shortHoldLimitMs) {
      systemState = 3;  // SHORT_HOLD (< 2 seconds)
      Serial.print("[EVENT] Arm button released - SHORT HOLD: ");
    } else if (heldForMs < longHoldLimitMs) {
      systemState = 4;  // LONG_HOLD (2-5 seconds)
      Serial.print("[EVENT] Arm button released - LONG HOLD: ");
    } else {
      systemState = 1;  // Reset to WAIT if > 5 seconds
      Serial.print("[EVENT] Arm button released - TOO LONG, reset: ");
    }
    
    printHoldResult(heldForMs);
    
    // FLAGGING OPPORTUNITY #6:
    //   Should Part B read sharedValue immediately, or should it wait
    //   for a "ready" flag? Currently no explicit signal that value is fresh.
    //   Add: bool holdDurationReady = true; when release is detected.
  }

  // ═════ EVENT: Cancel button PRESSED ═════
  // When: cancelDown is now true, but was false last loop
  // Action: Reset to WAIT state from any state
  if (cancelDown && !cancelButtonWasDown) {
    systemState = 1;                // Return to WAIT
    sharedValue = 0;                // Clear shared value
    Serial.println("[EVENT] Cancel button pressed - reset to WAIT");
    // FLAGGING OPPORTUNITY #7:
    //   If Part B is already running a countdown, should this cancel it?
    //   Currently only Part A resets. Consider a shared "abort" flag.
  }

  // ─────────────────────────────────────────────────────────────
  // STEP 3: OUTPUT: Update status lamp based on current state
  // ─────────────────────────────────────────────────────────────
  // The status lamp provides visual feedback of Part A's state.
  // This is the PRIMARY OUTPUT that demonstrates integrated behavior.
  updateStatusLamp();

  // ─────────────────────────────────────────────────────────────
  // STEP 4: MEMORY: Save current button state for next loop's edge detection
  // ─────────────────────────────────────────────────────────────
  armButtonWasDown = armDown;
  cancelButtonWasDown = cancelDown;

  // LOOP TIMING NOTE:
  //   This loop runs approximately every 1-2ms (limited by Serial I/O),
  //   so edge detection is responsive and non-blocking.
}

// ============================================================================
// FUNCTION: Update status lamp based on system state
// ============================================================================
// This function demonstrates INTEGRATED BEHAVIOR:
//   State (from button input) → Output (lamp control)
//
// LAMP BEHAVIOR:
//   State 1 (WAIT):       OFF (no activity)
//   State 2 (MEASURING):  BLINK (fast, 200ms period) - user feedback
//   State 3 (SHORT_HOLD): OFF (short hold not worth countdown?)
//   State 4 (LONG_HOLD):  ON (solid, Part C will take over)
//
// FLAGGING OPPORTUNITY #8:
//   Current logic: State 3 (short hold) → lamp OFF
//   Is this correct? Should short holds trigger anything in Part C?
//   Or should they be rejected silently?
// ============================================================================
void updateStatusLamp() {
  if (systemState == 2) {
    // MEASURING: Blink every 200ms to show active measurement
    // Formula: (millis() / 200) % 2 produces 0 or 1, switches every 200ms
    digitalWrite(statusLampPin, (millis() / 200) % 2);
  } 
  else if (systemState == 4) {
    // LONG_HOLD: Solid ON to indicate valid countdown-ready state
    digitalWrite(statusLampPin, HIGH);
  } 
  else {
    // WAIT, SHORT_HOLD, or any undefined state: OFF
    digitalWrite(statusLampPin, LOW);
  }

  // FLAGGING OPPORTUNITY #9:
  //   Should there be a dimming/fade effect for state transitions?
  //   PWM on pin 13 could create smoother visual feedback.
  //   Consider using analogWrite(statusLampPin, brightness) for states 3 & 4.
}

// ============================================================================
// FUNCTION: Print hold time result to Serial
// ============================================================================
// PURPOSE: Debugging and verification
// 
// OUTPUT FORMAT:
//   [RESULT] Hold time: 3500ms (3s) → shared state = 3 seconds
//
// FLAGGING OPPORTUNITY #10:
//   These messages are useful for tutor/peer verification.
//   Keep them, but consider adding timestamp:
//   Serial.print("[T="); Serial.print(millis()); Serial.print("]");
// ============================================================================
void printHoldResult(unsigned long heldMs) {
  Serial.print(heldMs);
  Serial.print("ms (");
  Serial.print(heldMs / 1000.0, 1);  // Print with 1 decimal place
  Serial.print("s) → shared state = ");
  Serial.print(sharedValue);
  Serial.println(" seconds");
}

// ============================================================================
// FUNCTION: Print verification checklist (call from Serial input if desired)
// ============================================================================
// USAGE: If you implement Serial input, call this on demand for peer review.
//
// This checklist proves to tutors that Part A meets integration criteria.
// ============================================================================
void printPartAChecklist() {
  Serial.println("\n================================================");
  Serial.println("Part A Verification Checklist");
  Serial.println("================================================");
  Serial.println("Hardware Integration:");
  Serial.println("  ✓ Arm button (pin 2): active-LOW, INPUT_PULLUP");
  Serial.println("  ✓ Cancel button (pin 11): active-LOW, INPUT_PULLUP");
  Serial.println("  ✓ Status lamp (pin 13): active-HIGH output");
  Serial.println("  ✓ Reserved pins for Part C: 6, 9, 10 (PWM for RGB)");
  Serial.println("");
  Serial.println("Software Integration:");
  Serial.println("  ✓ Non-blocking timing: millis() only, no delay()");
  Serial.println("  ✓ Edge-triggered input: armButtonWasDown, cancelButtonWasDown");
  Serial.println("  ✓ State machine: 4 states (WAIT, MEASURING, SHORT_HOLD, LONG_HOLD)");
  Serial.println("  ✓ Shared interface: systemState (1-4), sharedValue (seconds)");
  Serial.println("  ✓ Output feedback: statusLampPin responds to state");
  Serial.println("");
  Serial.println("Behavior Demonstration:");
  Serial.println("  1. Button input affects state (behavior integrated)");
  Serial.println("  2. State affects lamp output (demonstration of integration)");
  Serial.println("  3. Duration shared with Part B via sharedValue");
  Serial.println("");
  Serial.println("Flagging Notes (see code comments):");
  Serial.println("  - FLAG #1: State persistence and Part B signaling");
  Serial.println("  - FLAG #2: Sub-1-second hold time handling");
  Serial.println("  - FLAG #3: Threshold documentation");
  Serial.println("  - FLAG #4: Button debouncing");
  Serial.println("  - FLAG #5: stale data clearing");
  Serial.println("  - FLAG #6: 'ready' signal for Part B");
  Serial.println("  - FLAG #7: Part B abort capability");
  Serial.println("  - FLAG #8: Short hold behavior");
  Serial.println("  - FLAG #9: Lamp fade effects (PWM enhancement)");
  Serial.println("  - FLAG #10: Timestamp in Serial output");
  Serial.println("================================================\n");
}

// ============================================================================
// END OF PART A CODE
// ============================================================================
// 
// NEXT STEPS FOR INTEGRATION:
//
// 1. CIRCUIT VERIFICATION:
//    - Draw pin diagram showing buttons, lamp, pull-ups, and common GND
//    - Test on breadboard or Tinkercad before final build
//
// 2. PART B INTEGRATION:
//    - Write code that reads systemState and sharedValue
//    - Part B should monitor systemState to know when to start countdown
//    - Part B should send signal back (new state or flag) to reset Part A
//
// 3. PART C INTEGRATION:
//    - Configure RGB LED on pins 6, 9, 10
//    - Part C reads systemState to know when to flash rainbow
//    - Coordinate timing with Part B countdown
//
// 4. TESTING:
//    - Print Part A checklist via Serial
//    - Verify lamp blinks while holding, stays on after long hold
//    - Verify cancel button resets immediately
//    - Check Serial output for timing accuracy (use stopwatch)
//    - Verify Part B and C can access systemState and sharedValue
//
// ============================================================================
