/* USER GUIDE

Volume control:
'o' - lower volume
'p' - raise volume
'm' - toggle mute

Other controls (<setting> + <value> + <'e'>):
'b' - set tempo
'v' - set key

Start/Stop:
'a' - start
's' - stop

't' - Toggle conductor/musician

IMPORTANT: Only use tap tempo in conductor mode (Default mode)

*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include "sioTinyTimber.h"
#include "constants.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>

#define TONE_DEADLINE 100
#define MAX_VOLUME 45

typedef struct {
    Object super;
    char c;
    int currentMelodyIndex;
    int tempo;
    int key;
    char buff[8];
    Time tempoBurst[3];
    int buff_index;
    int tempo_index;
    bool isPlaying;
    bool stateOfButton;
    Timer timer;
    Timer longTimer;

    char set_check; //0 = default, 1 = set tempo, 2 = set key
    bool isReady;
} MusicPlayer;
typedef struct {
    Object super;

    int period;
    int volume;
    bool high;
    bool mute;
    bool stop;
    
} ToneGenerator;

MusicPlayer musicPlayer = { initObject(), ' ', 0, 120, 0, {}, {}, 0, -1, false, false, initTimer(), initTimer(), 0, true};
ToneGenerator toneGenerator = { initObject(), 500, 1, false, false, false};

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;


// Function Declarations
void reader(MusicPlayer*, int);
void receiver(MusicPlayer*, int);
void loopReceiver(MusicPlayer*, int);
void conductor(MusicPlayer*, int);
void loopConductor(MusicPlayer*, int);
void buttonOld(MusicPlayer*, int);
void buttonOld2(MusicPlayer*, int);
void button(MusicPlayer*, int);
void checkLongPress(MusicPlayer*, int);
void startApp(MusicPlayer*, int);

void start(ToneGenerator*, int);
void stop(ToneGenerator*, int);
void enablePlay(ToneGenerator*, int);

int lowerVolume(ToneGenerator*, int);
int raiseVolume(ToneGenerator*, int);
void mute(ToneGenerator*, int);
void setPeriod(ToneGenerator*, int);

int averageTempo(Time*);
int validTempoBurst(Time*);

// Communication 
Serial sci0c = initSerial(SCI_PORT0, &musicPlayer, conductor); // conductor callback
Serial sci0m = initSerial(SCI_PORT0, &musicPlayer, loopConductor); // musician callback
Can can0m = initCan(CAN_PORT0, &musicPlayer, receiver); // conductor callback
Can can0c = initCan(CAN_PORT0, &musicPlayer, loopReceiver); // musician callback

SysIO sio0 = initSysIO(SIO_PORT0, &musicPlayer, button); // button callback

// Function Definitions
void playMelody(MusicPlayer* self, int unused) {
    if (!self->isPlaying) { // Stop melody
        self->isReady = true; // Signal playMelody will no longer execute
        return;
    }

    // Configure tone generator
    Time beatLength = MSEC(1000 * 60 / self->tempo);
    Time toneLength = beatLength * toneLengthFactor[self->currentMelodyIndex];
    int currentToneIndex = melody[self->currentMelodyIndex];
    int currentPeriod = period[currentToneIndex + 10 + self->key];
    SYNC(&toneGenerator, setPeriod, currentPeriod);
    SYNC(&toneGenerator, enablePlay, 0);
    
    BEFORE(MSEC(1), &toneGenerator, start, 0); // Start tone
    SEND(toneLength - MSEC(50), MSEC(1), &toneGenerator, stop, 0); // End tone
    SEND(toneLength, MSEC(1), self, playMelody, 0); // Call to play next note in melody

    // Blinking
    switch (blinkCountFactor[self->currentMelodyIndex]) {
    case 2: // half note
       SEND(beatLength, MSEC(1), &sio0, sio_write, 0);
       SEND(beatLength + beatLength/2, MSEC(1), &sio0, sio_write, 1);
    case 1: // on beat
       SEND(0, MSEC(1), &sio0, sio_write, 0);
       SEND(beatLength/2, MSEC(1), &sio0, sio_write, 1);
    }

    // Increment melody index
    self->currentMelodyIndex = (self->currentMelodyIndex + 1) % 32;
}

void start(ToneGenerator* self, int not_used) {
    self->high = !self->high;

    if (self->high && !self->mute) 
        *addr_dac = (self->volume);
    else
        *addr_dac = 0;

    if (!self->stop)
        SEND(USEC(self->period), USEC(TONE_DEADLINE), self, start, 0);
}

void stop(ToneGenerator* self, int unused){
    self->stop = true;
}

void enablePlay(ToneGenerator* self, int unused) {
    self->stop = false;
}

int lowerVolume(ToneGenerator* self, int not_used) {
    if (self->volume > 0)
        self->volume--;
    return self->volume;
}

int raiseVolume(ToneGenerator* self, int not_used) {
    if (self->volume < MAX_VOLUME)
        self->volume++;
    return self->volume;
}

void mute(ToneGenerator* self, int not_used) {
    self->mute = !self->mute;
}

void setPeriod(ToneGenerator* self, int period) {
    self->period = period;
}

void conductor(MusicPlayer *self, int c){
    CANMsg msg;
    msg.nodeId = 1;
    msg.length = 0;

    int currentVolume;
    char volume[24];

    switch ((char)c) {
        case '0'...'9':
            case '-':
                if(self->set_check != 0){
                    SCI_WRITECHAR(&sci0c, (char)c);
                    self->buff[self->buff_index++] = (char)c;
                }
                return;
        case 'o': //Lower volume
            msg.msgId = 'o';
            currentVolume = SYNC(&toneGenerator, lowerVolume, 0);
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0c, volume);
            break;
        case 'p': //Increase volume
            msg.msgId = 'p';
            currentVolume = SYNC(&toneGenerator, raiseVolume, 0);         
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0c, volume);
            break;
        case 'm': //Mute
            msg.msgId = 'm';
            ASYNC(&toneGenerator, mute, 0);
            SCI_WRITE(&sci0c, "Mute-toggle\n");
            break;
        case 'a': //Play
            if (self->isPlaying) return;
            if (!self->isReady) return;
            self->isPlaying = true;
            msg.msgId = 'a';
            self->currentMelodyIndex = 0;
            ASYNC(&musicPlayer, playMelody, 0);
            SCI_WRITE(&sci0c, "Play\n");
            break;
        case 's': //Stop
            if (!self->isPlaying) return;
            self->isPlaying = false;
            self->isReady = false;
            msg.msgId = 's';
            SYNC(&toneGenerator, stop, 0);
            SCI_WRITE(&sci0c, "Stop\n");
            break;
        case 't': //Toggle conductor/musician
            INSTALL(&sci0m, sci_interrupt, SCI_IRQ0);
            SCI_INIT(&sci0m);
            INSTALL(&can0m, can_interrupt, CAN_IRQ0);
            CAN_INIT(&can0m);
            SCI_WRITE(&sci0m, "Now entering mucisian mode\n");
            return;
        case 'b': //Set tempo
            self->set_check = 1;
            SCI_WRITE(&sci0c, "New tempo: ");
            return;
        case 'v': //Set key
            self->set_check = 2;
            SCI_WRITE(&sci0c, "New key: ");
            return;
        case 'e': //Parse input
            bool clearReturn = false;
            switch(self->set_check) { // What mode are we in?
            case 0: 
                clearReturn = true;
                break;
            case 1: // Set tempo
                self->buff[self->buff_index++] = '\0';
                int newTempo = atoi(self->buff);
                if (newTempo < 30 && newTempo > 240){
                    SCI_WRITE(&sci0c, "\nTempo must be between 60 and 240!");
                    clearReturn = true;
                } else {
                    self->tempo = newTempo;
                    msg.msgId = 'b';
                }
                break;
            case 2: // Set key
                self->buff[self->buff_index++] = '\0';
                int newKey = atoi(self->buff);
                if (newKey > 5 && newKey < -5){
                    SCI_WRITE(&sci0c, "\nKey must be between -5 and 5");
                    clearReturn = true;
                } else {
                    self->key = newKey;
                    msg.msgId = 'v';
                }
                break;
            }

            // copy content to buffer
            if (self->set_check != 0 && !clearReturn) {
                msg.length = self->buff_index;
                strcpy((char*)msg.buff, self->buff);
            }
            
            // Clear buffer
            memset(self->buff, 0, sizeof self->buff);
            self->buff_index = 0;
            self->set_check = 0;
            SCI_WRITECHAR(&sci0c, '\n');
            if (clearReturn) return; // Do not send CAN message
            break;

        default:
            return;
    }
    CAN_SEND(&can0c, &msg);
}

void loopConductor(MusicPlayer *self, int c){
    CANMsg msg;
    msg.nodeId = 1;
    msg.length = 0;

    switch ((char)c) {
        case '0'...'9':
            case '-':
                if(self->set_check != 0){
                    SCI_WRITECHAR(&sci0m, (char)c);
                    self->buff[self->buff_index++] = (char)c;
                }
                return;

        case 'o': //Lower volume
            msg.msgId = 'o';
            break;

        case 'p': //Increase volume
            msg.msgId = 'p';          
            break;

        case 'm': //Mute
            msg.msgId = 'm';
            break;

        case 'a': //Play
            if (self->isPlaying) return;
            if (!self->isReady) return;
            msg.msgId = 'a';
            break;

        case 's': //Stop
            if (!self->isPlaying) return;
            msg.msgId = 's';
            break;

        case 't': //Toggle conductor/musician    
            INSTALL(&sci0c, sci_interrupt, SCI_IRQ0);
            SCI_INIT(&sci0c);
            INSTALL(&can0c, can_interrupt, CAN_IRQ0);
            CAN_INIT(&can0c);
            SCI_WRITE(&sci0c, "Now entering conductor mode\n");
            return;

        case 'b': //Set tempo
            self->set_check = 1;
            SCI_WRITE(&sci0m, "New tempo: ");
            return;

        case 'v': //Set key
            self->set_check = 2;
            SCI_WRITE(&sci0m, "New key: ");
            return;

        case 'e': //Parse numerical input
            bool clearReturn = false;
            switch (self->set_check) { // Check mode
            case 0: // No mode, clear buffer and return
                clearReturn = true;
            case 1: // Set tempo
                self->buff[self->buff_index++] = '\0';
                int newTempo = atoi(self->buff);
                if (newTempo < 30 && newTempo > 300){
                    SCI_WRITE(&sci0m, "\nTempo must be between 30 and 300!\n");
                    clearReturn = true; // Invalid, clear buffer and return
                } else {
                    msg.msgId = 'b';
                }
                break;
            case 2: // Set key
                self->buff[self->buff_index++] = '\0';
                int newKey = atoi(self->buff);
                if (newKey > 5 && newKey < -5){
                    SCI_WRITE(&sci0m, "\nKey must be between -5 and 5\n");
                    clearReturn = true;  // Invalid, clear buffer and return
                } else {
                    msg.msgId = 'v';
                }
                break;
            }

            // Copy buffer to message
            if (self->set_check != 0 && !clearReturn) {
                msg.length = self->buff_index;
                strcpy((char*)msg.buff, self->buff);
            }

            // Clear buffer
            memset(self->buff, 0, sizeof self->buff);
            self->buff_index = 0;
            self->set_check = 0;
            SCI_WRITECHAR(&sci0m, '\n');
            if (clearReturn) return;
            break;
        
        default:
            return;
    }

    CAN_SEND(&can0m, &msg);
}

void receiver(MusicPlayer *self, int unused){
    CANMsg msg;
    CAN_RECEIVE(&can0m, &msg);
    SCI_WRITE(&sci0m, "Can msg received: ");
    SCI_WRITE(&sci0m, msg.buff);

    int currentVolume;
    char volume[24];

    switch ((int)msg.msgId) {

        case 'o': //Lower volume
            currentVolume = SYNC(&toneGenerator, lowerVolume, 0);
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0m, volume);
            break;

        case 'p': //Increase volume
            currentVolume = SYNC(&toneGenerator, raiseVolume, 0);          
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0m, volume);
            break;

        case 'm': //Mute
            ASYNC(&toneGenerator, mute, 0);
            SCI_WRITE(&sci0m, "Mute-toggle\n");
            break;

        case 'a': //Play
            if (self->isPlaying) return;
            if (!self->isReady) return;
            self->isPlaying = true;
            self->currentMelodyIndex = 0;
            ASYNC(&musicPlayer, playMelody, 0);
            SCI_WRITE(&sci0m, "Play\n");
            break;

        case 's': //Stop
            if (!self->isPlaying) return;
            self->isPlaying = false;
            self->isReady = false;
            SYNC(&toneGenerator, stop, 0);
            SCI_WRITE(&sci0m, "Stop\n");
            break;

        case 'b': //Set tempo
            int newTempo = atoi((char*)msg.buff);
            self->tempo = newTempo;
            break;

        case 'v': //Set key
            int newKey = atoi((char*)msg.buff);
            self->key = newKey;
            break;

    }
} 

void loopReceiver(MusicPlayer *self, int unused){
    CANMsg msg;
    CAN_RECEIVE(&can0c, &msg);
    SCI_WRITE(&sci0c, "Can msg received of type '");
    
    switch ((char)msg.msgId)
    {
    case 'o':
    SCI_WRITE(&sci0c, "Increase volume");
        break;
    case 'p':
    SCI_WRITE(&sci0c, "Decrease volume");
        break;
    case 'm':
    SCI_WRITE(&sci0c, "Mute toggle");
        break;
    case 'a':
    SCI_WRITE(&sci0c, "Play");
        break;
    case 's':
    SCI_WRITE(&sci0c, "Stop");
        break;
    case 'b':
    SCI_WRITE(&sci0c, "Tempo change");
        break;
    case 'v':
    SCI_WRITE(&sci0c, "Key change");
        break;
    default:
    SCI_WRITE(&sci0c, "Unknown");
        break;
    }
    
    SCI_WRITE(&sci0c, "' containing payload: '");
    SCI_WRITE(&sci0c, msg.buff);
    SCI_WRITE(&sci0c, "'\n");

} 

void buttonOld(MusicPlayer *self, int unused) {
    char timePrint[45];
    
    self->stateOfButton = SIO_READ(&sio0);
    SIO_TRIG(&sio0, !self->stateOfButton);

    //Pressed
    if(!self->stateOfButton) {
        T_RESET(&self->longTimer);
        SEND(SEC(1), MSEC(1), self, checkLongPress, 0);
        return;
    }
    
    //Released
    Time diff = T_SAMPLE(&self->longTimer);

    if (diff < MSEC(100)) return; // Filter contact bounces

    if (diff >= MSEC(1000)) {
        // Long press
        // Express time in s
        snprintf(timePrint, 40, "Button long-pressed for %d s\n", SEC_OF(diff));
    } else {
        // Momentary press
        // Express time in ms
        snprintf(timePrint, 40, "Button short-pressed for %d ms\n", MSEC_OF(diff));
    }
    SCI_WRITE(&sci0c, timePrint);
}

void buttonOld2(MusicPlayer *self, int unused) {
    char timePrint[45];
    
    self->stateOfButton = SIO_READ(&sio0);
    SIO_TRIG(&sio0, !self->stateOfButton);

    //Pressed
    if(!self->stateOfButton) {
        T_RESET(&self->longTimer);
        SEND(SEC(1), MSEC(1), self, checkLongPress, 0);
        return;
    }
    
    //Released
    Time diffLong = T_SAMPLE(&self->longTimer);
    Time diff = T_SAMPLE(&self->timer);
    T_RESET(&self->timer);

    if (diffLong < MSEC(100)) return; // Filter contact bounces

    if (diffLong >= MSEC(1000)) {
        // Long press
        // Express time in s
        snprintf(timePrint, 40, "Button long-pressed for %d s\n", SEC_OF(diffLong));
    } else {
        // Momentary press
        // Express time in ms
        snprintf(timePrint, 40, "Time since last press: %d ms\n", SEC_OF(diff)*1000 + MSEC_OF(diff));
    }
    SCI_WRITE(&sci0c, timePrint);
}

void button(MusicPlayer *self, int unused) {

    self->stateOfButton = SIO_READ(&sio0);  // Remember button state
    SIO_TRIG(&sio0, !self->stateOfButton);  // Flip trigger

    if (!self->stateOfButton) { // Button pressed
        T_RESET(&self->longTimer); // Reset timer for button pressed time
        return;
    }

    // Released
    Time downTime = T_SAMPLE(&self->longTimer); 

    if (downTime >= SEC(2)) {
        // Prepare and send CAN message
        CANMsg msg;
        msg.nodeId = 1;
        msg.msgId = 'b';
        snprintf((char*)msg.buff, 4, "%d", 120);
        msg.length = 3;
        CAN_SEND(&can0c, &msg);

        self->tempo = 120;
        memset(self->tempoBurst, 0, sizeof self->tempoBurst);
        self->tempo_index = -1;
        SCI_WRITE(&sci0c, "Tempo reset to 120 BPM\n");
        return;
    }

    if (self->tempo_index == -1) { // First press in tempo burst series
        T_RESET(&self->timer); // Reset timer until next release
        self->tempo_index++; // Increment tempo burst index
        return;
    }

    // In the middle of a tempo burst
    Time diff = T_SAMPLE(&self->timer); // Time since last release
    T_RESET(&self->timer); // Reset timer

    if (diff < MSEC(100)) return; // Filter contact bounces

    self->tempoBurst[self->tempo_index] = diff; // Insert time

    if (self->tempo_index < 2) {
        self->tempo_index++; // Increment until next release
        return;
    }

    if (!validTempoBurst(self->tempoBurst)) {
        SCI_WRITE(&sci0c, "Need steadier tempo!\n");
        memset(self->tempoBurst, 0, sizeof self->tempoBurst); // Clear tempo burst array
        self->tempo_index = -1; // Reset tempo index
        return;
    }

    // Proper tempoBurst, find and set tempo
    int newTempo = averageTempo(self->tempoBurst); // Find tempo
    
    if (newTempo < 30 || newTempo > 300) {
        SCI_WRITE(&sci0c, "Tempo must be between 30 and 300 BPM\n");
        memset(self->tempoBurst, 0, sizeof self->tempoBurst); // Clear tempo burst array
        self->tempo_index = -1; // Reset tempo index
        return;
    }

    // Prepare and send CAN message
    CANMsg msg;
    msg.nodeId = 1;
    msg.msgId = 'b';
    snprintf((char*)msg.buff, 4, "%d", newTempo);
    if (newTempo < 100) msg.length = 2;
    else msg.length = 3;
    CAN_SEND(&can0c, &msg);

    self->tempo = newTempo; // Set new tempo
    char tempoPrint[29];
    snprintf(tempoPrint, 40, "New tempo: %d BPM\n", newTempo);
    SCI_WRITE(&sci0c, tempoPrint);
    memset(self->tempoBurst, 0, sizeof self->tempoBurst); // Clear tempo burst array
    self->tempo_index = -1; // Reset tempo index
    return;
}

int averageTempo(Time *tempoBurst) {
    Time averageBeatLength = (tempoBurst[0]
        +tempoBurst[1]
        +tempoBurst[2])/3;
    return (int) 60000 / (1000*SEC_OF(averageBeatLength) + MSEC_OF(averageBeatLength));
} 

int validTempoBurst(Time *tempoBurst) {
    for (int i = 0; i < 3; i++) {
    Time diff = tempoBurst[i];
    Time variance = tempoBurst[(i + 1) % 3] - diff; // Difference between last duration

    if (variance > MSEC(100) || variance < -MSEC(100)) { // 100 ms limit
        return 0;
    }}
    return 1;
}

void checkLongPress(MusicPlayer *self, int unused) {
    Time diff = T_SAMPLE(&self->longTimer);
    if (diff >= SEC(1) && !self->stateOfButton) {
        SCI_WRITE(&sci0c, "Now entering LONG-PRESS-MODE\n");
    }
}

int main() {
    INSTALL(&sci0c, sci_interrupt, SCI_IRQ0);
    INSTALL(&can0c, can_interrupt, CAN_IRQ0);
    INSTALL(&sio0, sio_interrupt, SIO_IRQ0);
    TINYTIMBER(&musicPlayer, startApp, 0);
    return 0;
}

void startApp(MusicPlayer *self, int arg) {

    SCI_INIT(&sci0c);
    CAN_INIT(&can0c);
    SIO_INIT(&sio0);

    SCI_WRITE(&sci0c, "Application loaded in conductor mode\n");
}
