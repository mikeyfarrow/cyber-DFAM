#include <MIDI.h>

// uncomment the following line to run with print statements
#define SERIAL_DEBUG

/**
 * TODO: test DAC v/oct output
 */

// MIDI instance must be created in the global scope, not in setup()
MIDI_CREATE_DEFAULT_INSTANCE();

/******************************************/
/**** Input/Output PIN NUMBER MAPPINGS ****/
#define PIN_SWITCH 2 // digital IN, HIGH when controlled by MIDI start/stop; LOW otherwise
#define PIN_ADV 6    // digital OUT, pulses to advance the DFAM sequencer
#define PIN_VOCT A14 // analog OUT, the one true DAC
#define PIN_VEL 3    // digital PWM out

/**************************/
/**** Global CONSTANTS ****/
#define NUM_STEPS 8
#define PPQN 24
#define CC_CLOCK_MULT 0x46 // a random CC digitakt happens to send
#define CC_ALL_NOTES_OFF 123
#define MIDI_ROOT_NOTE 48  // 0x30, an octave below middle C

/**************************/
/**** Global VARIABLES ****/
enum State
{
   Play,
   Stop
} SEQ_STATE = Stop;
int CLOCK_COUNT = 0;
int CUR_DFAM_STEP = 0; // the number of the last DFAM step triggered
int CLOCK_DIV = 1;
int PULSES_PER_STEP = PPQN / CLOCK_DIV;
uint8_t MIDI_CHAN_DFAM = 1; // MIDI channel for playing DFAM in "8-voice mono-synth" mode
uint8_t MIDI_CHAN_A = 2;
uint8_t MIDI_CHAN_B = 3;
uint8_t LAST_DIV_CV = 0; // to keep track of clock division/multiplication
uint8_t SWITCH_STATE = 0;

/////////////////////////////////////////////////////////////
////////////////// ARDUINO BOILERPLATE //////////////////////
/////////////////////////////////////////////////////////////
void setup()
{
#ifdef SERIAL_DEBUG
   while (!Serial)
      ;
#endif

   // attach MIDI event callback functions
   MIDI.setHandleStart(handleStart);
   MIDI.setHandleStop(handleStop);
   MIDI.setHandleContinue(handleContinue);
   MIDI.setHandleClock(handleClock);
   MIDI.setHandleControlChange(handleCC);
   MIDI.setHandleNoteOn(handleNoteOn);
   MIDI.setHandleNoteOff(handleNoteOff);

   // configue GPIO pins
   pinMode(LED_BUILTIN, OUTPUT);
   pinMode(PIN_SWITCH, INPUT);
   pinMode(PIN_ADV, OUTPUT);
   pinMode(PIN_VOCT, OUTPUT);
   pinMode(PIN_VEL, OUTPUT);

   // read the state of the switch to determine if we should
   // listen to or ignore MIDI start/stop/continue messages
   SWITCH_STATE = digitalRead(PIN_SWITCH);
   SEQ_STATE = SWITCH_STATE ? Play : Stop;
   CUR_DFAM_STEP = SWITCH_STATE ? 0 : 1;

   // Initiate MIDI communications, listen to all channels
   MIDI.begin(MIDI_CHANNEL_OMNI);
}

/**
 * This function will automatically be called repeatedly.
 * Each time, we check for new MIDI messages and then read the state of the
 * Teensy's inputs (i.e. the switch).
 */
void loop()
{
   MIDI.read();
   checkModeSwitch();
}

void printStateInfo()
{
#ifdef SERIAL_DEBUG
   Serial.printf("\t\t\t\tSEQ_STATE, CLOCK_COUNT, CUR_DFAM_STEP, SWITCH_STATE\n");
   Serial.printf("\t\t\t\t%d\t\t%d\t\t%d\t%d\n", SEQ_STATE, CLOCK_COUNT, CUR_DFAM_STEP, SWITCH_STATE);
#endif
}

void checkModeSwitch()
{
   uint8_t curSwitch = digitalRead(PIN_SWITCH);
   if (curSwitch == SWITCH_STATE)
      return;

   SWITCH_STATE = curSwitch;
   CLOCK_COUNT = 0;

#ifdef SERIAL_DEBUG
   Serial.printf("Switch state changed to: %d\n", SWITCH_STATE);
   printStateInfo();
#endif

   int stepsLeft = stepsBetween(CUR_DFAM_STEP, 1) + 1;
   burstOfPulses(PIN_ADV, stepsLeft);

   if (SWITCH_STATE)
   {
      // SEQ_STATE = Play;
      CUR_DFAM_STEP = 0;
   }
   else
   {
      CUR_DFAM_STEP = 1;
      handleStop();
   }
}

//////////////////////////////////////////////////
////////////////// HARDWARE I/O //////////////////
//////////////////////////////////////////////////
void sendPulse(uint8_t pin)
{
   digitalWrite(pin, HIGH);
   digitalWrite(pin, LOW);
}

void burstOfPulses(uint8_t pin, int numPulses)
{
#ifdef SERIAL_DEBUG
   Serial.printf("Advancing DFAM sequencer %d steps\n", numPulses);
#endif

   for (int i = 0; i < numPulses; i++)
   {
      sendPulse(pin);
   }
}

/////////////////////////////////////////////////////////////
////////////////// MIDI EVENT HANDLERS //////////////////////
/////////////////////////////////////////////////////////////

/**
 * Called on every MIDI clock event.
 *
 * MIDI beat clock is 24 PPQN so this should get called 24 times per step
 */
void handleClock()
{
   if (SEQ_STATE == Play && SWITCH_STATE)
   {
      // only count clock pulses while sequence is playing and mode is selected
      CLOCK_COUNT = CLOCK_COUNT % PULSES_PER_STEP + 1;

      if (CLOCK_COUNT == 1) // we have a new step
      {
         CUR_DFAM_STEP = CUR_DFAM_STEP % NUM_STEPS + 1;
         sendPulse(PIN_ADV);

#ifdef SERIAL_DEBUG
         Serial.printf("New quarter note: DFAM=%d", CUR_DFAM_STEP);
         Serial.println();
#endif
      }
   }
}

/**
 * Called on every MIDI "start" event.
 */
void handleStart()
{
#ifdef SERIAL_DEBUG
   Serial.println("Started");
#endif

   if (SWITCH_STATE)
   {
      digitalWrite(LED_BUILTIN, HIGH);

      int stepsLeft = stepsBetween(CUR_DFAM_STEP, 1);
      burstOfPulses(PIN_ADV, stepsLeft);
      SEQ_STATE = Play;
      CLOCK_COUNT = 0;
      CUR_DFAM_STEP = 0;
   }
}

/**
 * Called on every MIDI "stop" event.
 */
void handleStop()
{
#ifdef SERIAL_DEBUG
   Serial.println("Stopped");
#endif

   if (SEQ_STATE == Stop)
   {
#ifdef SERIAL_DEBUG
      Serial.println("Stopped again, resetting state");
#endif
      // reset the state of the sequencer
      // if we get a STOP when it is already stopped
      // give the DFAM sequencer a chance to re-sync
      CUR_DFAM_STEP = digitalRead(PIN_SWITCH) ? 0 : 1;
   }

   SEQ_STATE = Stop;
   digitalWrite(LED_BUILTIN, LOW);
}

/**
 * Called on every MIDI "Continue" event.
 */
void handleContinue()
{
#ifdef SERIAL_DEBUG
   Serial.println("Continued");
#endif

   digitalWrite(LED_BUILTIN, HIGH);
   SEQ_STATE = Play;
}

/**
 * Called for every MIDI CC message
 */
void handleCC(uint8_t channel, uint8_t number, uint8_t value)
{
#ifdef SERIAL_DEBUG
   Serial.printf("CC received\tChannel=%x\tnum=%x\tval=%x\n", channel, number, value);
#endif

   if (number == CC_ALL_NOTES_OFF)
   {
      handleStop();
      return;
   }

  //  if (number == CC_CLOCK_MULT)
  //  {
  //     uint8_t prev = LAST_DIV_CV;
  //     LAST_DIV_CV = value;
  //     if (LAST_DIV_CV > prev)
  //     {
  //        CLOCK_DIV *= 2;
  //     }
  //     else
  //     {
  //        CLOCK_DIV /= 2;
  //     }
  //     CLOCK_DIV = min(64, CLOCK_DIV);
  //     CLOCK_DIV = max(1, CLOCK_DIV);
  //     PULSES_PER_STEP = PPQN / CLOCK_DIV;
  //     Serial.printf("Previous div=%x\tCurrent div=%d\n", prev, CLOCK_DIV);
  //  }
}

/**
 * returns the number of steps the DFAM sequencer would need
 * to advance to get from start to end
 */
int stepsBetween(int start, int end)
{
   if (start == 0 || start == end)
      return NUM_STEPS - 1;

   int stepsLeft = end - start - 1;
   if (end < start)
      stepsLeft += NUM_STEPS;

   return stepsLeft;
}

/**
 * Called on every MIDI "note on" event.
 */
void handleNoteOn(uint8_t ch, uint8_t note, uint8_t velocity)
{
#ifdef SERIAL_DEBUG
   Serial.printf("Note on\tCh. %x\tnote=%d (0x%x)\tvel=%d (0x%x)\n", ch, note, note, velocity, velocity);
#endif

   // return immediately if this message is on a channel we don't care about
   if (ch != MIDI_CHAN_DFAM && ch != MIDI_CHAN_A && ch != MIDI_CHAN_B)
      return;

   if (ch == MIDI_CHAN_DFAM && !SWITCH_STATE)
   {
      if (note >= MIDI_ROOT_NOTE && note < MIDI_ROOT_NOTE + NUM_STEPS)
      {
         int stepPlayed = note - MIDI_ROOT_NOTE + 1; // middle c => step one
         int stepsLeft = stepsBetween(CUR_DFAM_STEP, stepPlayed) + 1;

         // send the velocity
         analogWrite(PIN_VEL, velocity);

         /** TODO: this sometimes results in the envelopes triggering too early... i.e.
          *        the envelopes trigger before the pitch info arrives on DFAM's CV input
          *        (tested using doepfer quantizer) */

         // advance the DFAM's sequencer and then trigger the step
         burstOfPulses(PIN_ADV, stepsLeft);
         CUR_DFAM_STEP = stepPlayed;
      }
   }

   if (ch == MIDI_CHAN_A)
   {
      // will need to convert MIDI notes to V/oct CVs
      // send V/oct on primary v/oct output
      // send trig  '''
      // send vel  '''
   }

   if (ch == MIDI_CHAN_B)
   {
   }
}

/**
 * Called on every MIDI "note off" event.
 */
void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity)
{
#ifdef SERIAL_DEBUG
// Serial.printf("Note off\tChannel=%x\tnote=%x\tvel=%x\n", channel, note, velocity);
#endif
}
