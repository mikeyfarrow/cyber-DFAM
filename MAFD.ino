#include <MIDI.h>

/**
 * TODO: test the switch for start/stop, code may need to be revised?
 * TODO: test DAC v/oct output
 * TODO: handle MIDI CC 123 (All Notes Off) as a stop?
 */

// MIDI instance must be created in the global scope, not in setup()
MIDI_CREATE_DEFAULT_INSTANCE();

/******************************************/
/**** Input/Output PIN NUMBER MAPPINGS ****/
#define PIN_SWITCH   2   // digital IN, HIGH when controlled by MIDI start/stop; LOW otherwise
#define PIN_ADV      6   // digital OUT, pulses to advance the DFAM sequencer 
#define PIN_VOCT     A14 // analog OUT, the one true DAC
#define PIN_VEL      3   // digital PWM out

/**************************/
/**** Global CONSTANTS ****/
#define NUM_STEPS      8
#define PPQN           24
#define CLOCK_MULT_CC  0x46 // a random CC digitakt happens to send
#define MIDI_ROOT_NOTE 48   // 0x30, an octave below middle C
#define SERIAL_DEBUG        // remove this to run without print statements

/**************************/
/**** Global VARIABLES ****/
enum State { Play, Stop } SEQ_STATE       = Stop;
int                       CLOCK_COUNT     = 0;
int                       CUR_DFAM_STEP   = 0;
int                       CLOCK_DIV       = 4;
int                       PULSES_PER_STEP = PPQN / CLOCK_DIV;
int                       LAST_STEP       = 1; // last step of the sequencer when in midi trigger mode
uint8_t                   MIDI_CHAN_DFAM  = 1; // MIDI channel for playing DFAM in "8-voice mono-synth" mode
uint8_t                   MIDI_CHAN_A     = 2; 
uint8_t                   MIDI_CHAN_B     = 3;
uint8_t                   LAST_DIV_CV     = 0; // to keep track of clock division/multiplication
uint8_t                   SWITCH_STATE    = 0;

/////////////////////////////////////////////////////////////
////////////////// ARDUINO BOILERPLATE //////////////////////
/////////////////////////////////////////////////////////////
void setup()
{
  #ifdef SERIAL_DEBUG
  while (!Serial) ;
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

  // check on state of switch
  uint8_t curSwitch = digitalRead(PIN_SWITCH);
  if (curSwitch == SWITCH_STATE)
  {
    return; // nothing has changed, bail immediately
  }
  else
  {
    handleSwitchStateChange(curSwitch);
  }
}

void handleSwitchStateChange(uint8_t newState)
{
  #ifdef SERIAL_DEBUG
  Serial.printf("Switch state changed to: %d\n", newState);
  #endif

  SWITCH_STATE = newState;
  if (SWITCH_STATE)
  {
    CUR_DFAM_STEP = 0;
    LAST_STEP = 0;
  }
  else
  {
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
  if (SEQ_STATE == Play)
  {
    // only count clock pulses while sequence is playing
    CLOCK_COUNT =  CLOCK_COUNT % PULSES_PER_STEP + 1;

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

  digitalWrite(LED_BUILTIN, HIGH);

  // if we are on step 1 (or 0), advance the sequencer all the way around
  // otherwise just advance it by however many steps away from step one
  int stepsLeft = CUR_DFAM_STEP > 1
                      ? NUM_STEPS - CUR_DFAM_STEP
                      : NUM_STEPS - 1;
  SEQ_STATE = Play;
  CLOCK_COUNT = 0;
  CUR_DFAM_STEP = 0;

  burstOfPulses(PIN_ADV, stepsLeft);
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
    // reset the state of the sequencer
    // if we get a STOP when it is already stopped
    // give the DFAM sequencer a chance to re-sync
    CUR_DFAM_STEP = 0;
    LAST_STEP = 0;
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

  if (number == CLOCK_MULT_CC)
  {
    uint8_t prev = LAST_DIV_CV;
    LAST_DIV_CV = value;
    if (LAST_DIV_CV > prev)
    {
      CLOCK_DIV *= 2;
    }
    else
    {
      CLOCK_DIV /= 2;
    }
    CLOCK_DIV = min(64, CLOCK_DIV);
    CLOCK_DIV = max(1, CLOCK_DIV);
    PULSES_PER_STEP = PPQN / CLOCK_DIV;
    Serial.printf("Previous div=%x\tCurrent div=%d\n", prev, CLOCK_DIV);
  }
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

  if (ch == MIDI_CHAN_DFAM)
  {
    // check to see if the note being played is one of the eight notes that are
    // mapped to one of the DFAM's steps
    if (note >= MIDI_ROOT_NOTE && note < MIDI_ROOT_NOTE + NUM_STEPS)
    {
      int stepPlayed = note - MIDI_ROOT_NOTE + 1; // middle c => step one
      int stepsLeft = stepPlayed - LAST_STEP;
      if (stepsLeft < 1)
      {
        stepsLeft += NUM_STEPS;
      }

      // send the velocity
      analogWrite(PIN_VEL, velocity);

      /** TODO: this sometimes results in the envelopes triggering too early... i.e.
       *        the envelopes trigger before the pitch info arrives on DFAM's CV input
       *        (tested using doepfer quantizer) */
      // advance the DFAM's sequencer and thereby trigger the step 
      burstOfPulses(PIN_ADV, stepsLeft);
      LAST_STEP = stepPlayed;


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

