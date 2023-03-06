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
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <string.h>
#include "constants.h"

#define MAX_VOLUME 45
#define TONE_DEADLINE 45

typedef struct {
    Object super;
    char c;
    int currentMelodyIndex;
    int tempo;
    int key;
    char buff[8];
    int buff_index;
    int set_check;
    bool isPlaying;
    bool isReady;
} MusicPlayer;
typedef struct {
    Object super;
    int period;
    int volume;
    int value;
    bool high;
    bool stop;
    bool isReady;
    bool mute;
} ToneGenerator;

MusicPlayer musicPlayer = { initObject(), ' ', 0, 120, 0, {}, 0, 0, false, true};

ToneGenerator soprano = { initObject(), 500, 1, 0, false, false, true, false };
ToneGenerator alto = { initObject(), 500, 1, 0, false, false, true, false };
ToneGenerator tenor = { initObject(), 500, 1, 0, false, false, true, false };
ToneGenerator bass = { initObject(), 500, 1, 0, false, false, true, false };

ToneGenerator* harmonies[4] = { &soprano, &alto, &tenor, &bass };

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;

// Function Declarations
void conductor(MusicPlayer*, int);
void startApp(MusicPlayer*, int);

void start(ToneGenerator*, int);
void stop(ToneGenerator*, int);
void enablePlay(ToneGenerator*, int);
int lowerVolume(ToneGenerator*, int);
int raiseVolume(ToneGenerator*, int);
void mute(ToneGenerator*, int);
void setPeriod(ToneGenerator*, int);

// Communication 
Serial sci0 = initSerial(SCI_PORT0, &musicPlayer, conductor); // conductor callback

// Function Definitions
void playMelody(MusicPlayer* self, int unused) {
    if (!self->isPlaying) { // Stop melody
        self->isReady = true; // Signal playMelody will no longer execute
        return;
    }

    Time beatLength = MSEC(1000 * 60 / self->tempo) / 2;

    Tone** melody;
    ToneGenerator* voice;

    // Configure tone generator
    for (int i = 0; i < 4; i++) {
        melody = music[i];
        voice = harmonies[i];

        Tone* currentTone = melody[self->currentMelodyIndex];
        if (currentTone->mute) continue;
        int currentPeriod = midiToPeriod[currentTone->midiNoteValue + self->key];
        int toneLength = currentTone->length;

        SYNC(voice, setPeriod, currentPeriod);
        SYNC(voice, enablePlay, 0);
        
        BEFORE(MSEC(1), voice, start, 1); // Start tone
        SEND(beatLength*toneLength - MSEC(50), MSEC(1), voice, stop, 0); // End tone
    }
    
    SEND(beatLength, MSEC(1), self, playMelody, 0); // Call to play next note in melody

    // Increment melody index
    self->currentMelodyIndex = (self->currentMelodyIndex + 1) % 16;
}

void start(ToneGenerator* self, int init) {
    if (self->stop) {
        *addr_dac = *addr_dac - self->value;
        self->value = 0;
        self->isReady = true;
        return;
    }

    SEND(USEC(self->period), USEC(TONE_DEADLINE), self, start, 0);

    if (init) {
        self->isReady = false;
        self->high = 1;
        self->value = self->volume;
        *addr_dac += self->value;   
        return;
    }

    self->high = !self->high;

    if (self->mute) {
        *addr_dac = *addr_dac - self->value;
        self->value = 0;
        return;
    }
    
    if (self->high) 
        *addr_dac += 2*self->value;
    else
        *addr_dac -= 2*self->value;
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

    switch ((char)c) {
        case '0'...'9':
            case '-':
                if(self->set_check != 0){
                    SCI_WRITECHAR(&sci0, (char)c);
                    self->buff[self->buff_index++] = (char)c;
                }
                return;
        case 'a': //Play
            if (self->isPlaying) return;
            if (!self->isReady) return;
            self->isPlaying = true;
            self->currentMelodyIndex = 0;
            ASYNC(&musicPlayer, playMelody, 0);
            SCI_WRITE(&sci0, "Play\n");
            return;
        case 's': //Stop
            if (!self->isPlaying) return;
            self->isPlaying = false;
            self->isReady = false;
            SCI_WRITE(&sci0, "Stop\n");
            return;
        case 'b': //Set tempo
            self->set_check = 1;
            SCI_WRITE(&sci0, "New tempo: ");
            return;
        case 'v': //Set key
            self->set_check = 2;
            SCI_WRITE(&sci0, "New key: ");
            return;
        case 'e': //Parse input
            switch(self->set_check) { // What mode are we in?
            case 1: // Set tempo
                self->buff[self->buff_index++] = '\0';
                int newTempo = atoi(self->buff);
                if (newTempo < 60 && newTempo > 240){
                    SCI_WRITE(&sci0, "\nTempo must be between 60 and 240!");
                } else {
                    self->tempo = newTempo;
                }
                break;
            case 2: // Set key
                self->buff[self->buff_index++] = '\0';
                int newKey = atoi(self->buff);
                if (newKey > 5 && newKey < -5){
                    SCI_WRITE(&sci0, "\nKey must be between -5 and 5");
                } else {
                    self->key = newKey;
                }
                break;
            }
            
            // Clear buffer
            memset(self->buff, 0, sizeof self->buff);
            self->buff_index = 0;
            self->set_check = 0;
            SCI_WRITECHAR(&sci0, '\n');
            return;

        default:
            return;
    }
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
    TINYTIMBER(&musicPlayer, startApp, 0);
    return 0;
}

void startApp(MusicPlayer *self, int arg) {
    SCI_INIT(&sci0);
    *addr_dac = 128;
    SCI_WRITE(&sci0, "Application loaded\n");
}
