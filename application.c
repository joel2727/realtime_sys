/* USER GUIDE

Volume control:
'o' - lower volume
'p' - raise volume
'm' - toggle mute

Other controls (<setting> + <value> + <'e'>):
'b' - set tempo
'v' - set key

Workload control:
'q' - decrease workload
'w' - increase workload

Deadline:
'd' - deadline toggle

Start/Stop:
'a' - start
's' - stop

*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include  <stdbool.h>
#include "constants.h"
#include <time.h>
#include <string.h>

#define TONE_DEADLINE 100
#define MAX_VOLUME 45

#define BENCHMARK 1 //Uncomment for benchmarking


typedef struct {
    Object super;
    char c;
    int currentMelodyIndex;
    int tempo;
    int key;
    char buff[15];
    int buff_index;
    bool isPlaying;
    bool stop;

    char set_check; //0 = default, 1 = set tempo, 2 = set key
} MusicPlayer;
typedef struct {
    Object super;

    int period;
    int volume;
    bool high;
    bool mute;
    bool stop;
    
} ToneGenerator;

MusicPlayer musicPlayer = { initObject(), ' ', 0, 120, 0, {}, 0, false, 0, false};
ToneGenerator toneGenerator = { initObject(), 500, 1, false, false, false};

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;


// Function Declarations
void reader(MusicPlayer*, int);
void receiver(MusicPlayer*, int);
void controller(MusicPlayer*, int);
void startApp(MusicPlayer*, int);

void start(ToneGenerator*, int);
void stop(ToneGenerator*, int);
void enablePlay(ToneGenerator*, int);

int lowerVolume(ToneGenerator*, int);
int raiseVolume(ToneGenerator*, int);
void mute(ToneGenerator*, int);
void setPeriod(ToneGenerator*, int);

// Function Definitions

// Communication 
Serial sci0 = initSerial(SCI_PORT0, &musicPlayer, controller);

void playMelody(MusicPlayer* self, int unused){
    if (self->stop) return;

    Time beatLength = MSEC(1000 * 60 / self->tempo);
    Time toneLength = beatLength * toneLengthFactor[self->currentMelodyIndex];
    int currentToneIndex = melody[self->currentMelodyIndex];
    int currentPeriod = period[currentToneIndex + 10 + self->key];
    SYNC(&toneGenerator, setPeriod, currentPeriod);
    SYNC(&toneGenerator, enablePlay, 0);
    

    self->currentMelodyIndex = (self->currentMelodyIndex + 1) % 32;

    BEFORE(MSEC(1), &toneGenerator, start, 0);
    SEND(toneLength - MSEC(50), MSEC(1), &toneGenerator, stop, 0);
    SEND(toneLength, MSEC(1), self, playMelody, 0);
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

void controller(MusicPlayer *self, int c){
    int currentVolume;
    char volume[24];

    switch ((char)c) {
            case '0'...'9':
                case '-':
                    if(self->set_check != 0){
                        SCI_WRITECHAR(&sci0, (char)c);
                        self->buff[self->buff_index++] = (char)c;
                    }
                    break;
            case 'o': //Lower volume
                currentVolume = SYNC(&toneGenerator, lowerVolume, 0);

                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);
                break;
            case 'p': //Increase volume
                currentVolume = SYNC(&toneGenerator, raiseVolume, 0);          
                
                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);
                break;
            case 'm': //Mute
                ASYNC(&toneGenerator, mute, 0);
                SCI_WRITE(&sci0, "Mute-toggle\n");
                break;
            case 'a': //Play
                if (!self->isPlaying){
                    self->currentMelodyIndex = 0;
                    self->stop = false;
                    ASYNC(&musicPlayer, playMelody, 0);
                }
                self->isPlaying = true;
                SCI_WRITE(&sci0, "Play\n");
                break;
            case 's': //Stop
                self->isPlaying = false;
                self->stop = true;
                SYNC(&toneGenerator, stop, 0);
                SCI_WRITE(&sci0, "Stop\n");
                break;
            case 'b': //Set tempo
                self->set_check = 1;
                SCI_WRITE(&sci0, "New tempo: ");
                break;
            case 'v': //Set key
                self->set_check = 2;
                SCI_WRITE(&sci0, "New key: ");
                break;
            case 'e': //Parse input
                if(self->set_check == 1){ // Set tempo
                    self->buff[self->buff_index++] = '\0';
                    int newTempo = atoi(self->buff);
                    if (newTempo >= 60 && newTempo <= 240){
                        self->tempo = newTempo;
                    } else {
                        SCI_WRITE(&sci0, "\nTempo must be between 60 and 240!");
                    }
                }
                else if(self->set_check == 2){ // Set key
                    self->buff[self->buff_index++] = '\0';
                    int newKey = atoi(self->buff);
                    if (newKey <= 5 && newKey >= -5){
                        self->key = newKey;
                    } else {
                        SCI_WRITE(&sci0, "\nKey must be between -5 and 5");
                    }
                }
                memset(self->buff, 0, sizeof self->buff);
                self->buff_index = 0;
                self->set_check = 0;
                SCI_WRITECHAR(&sci0, '\n');
                break;
        }
} 

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);

    TINYTIMBER(&musicPlayer, startApp, 0);
    return 0;
}

void startApp(MusicPlayer *self, int arg) {

    SCI_INIT(&sci0);
    SCI_WRITE(&sci0, "Application loaded\n");
}
