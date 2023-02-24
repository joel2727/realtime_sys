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

enum msgTypes {InitPlay=1, LowerVol=2, IncreaseVol=3, ToggleMute=4, Play=5, Stop=6, Tempo=7, Key=8};


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
    int volume;

    char set_check; //0 = default, 1 = set tempo, 2 = set key
    char mode; //0 = default, 1 = conductor, 2 = musician
} MusicPlayer;
typedef struct {
    Object super;

    int period;
    int volume;
    bool high;
    bool mute;
    bool stop;
    
} ToneGenerator;

MusicPlayer musicPlayer = { initObject(), ' ', 0, 120, 0, {}, 0, false, false,1, 0, 0};
ToneGenerator toneGenerator = { initObject(), 500, 1, false, false, false};

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;


// Function Declarations
void reader(MusicPlayer*, int);
void receiver(MusicPlayer*, int);
void controller(MusicPlayer*, int);
void startApp(MusicPlayer*, int);

void start(ToneGenerator*, int);
void toggleStop(ToneGenerator*, int);
void enablePlay(ToneGenerator*, int);

int lowerVolume(ToneGenerator*, int);
int raiseVolume(ToneGenerator*, int);
void mute(ToneGenerator*, int);
void setPeriod(ToneGenerator*, int);

void sendCanMsg(MusicPlayer*, int);

// Function Definitions

// Communication 
Serial sci0 = initSerial(SCI_PORT0, &musicPlayer, controller);

// CAN Communication
Can can0 = initCan(CAN_PORT0, &musicPlayer, receiver);



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
    SEND(toneLength - MSEC(50), MSEC(1), &toneGenerator, toggleStop, 0);
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

void toggleStop(ToneGenerator* self, int unused){
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
                    if(self->mode == 2) return;
                    if(self->set_check != 0){
                        SCI_WRITECHAR(&sci0, (char)c);
                        self->buff[self->buff_index++] = (char)c;
                    }
                    break;
            case 'o': //Lower volume
                if(self->mode == 2) return;
                else if (self->mode == 1)
                    ASYNC(&musicPlayer, sendCanMsg, LowerVol);
                else {
                    currentVolume = SYNC(&toneGenerator, lowerVolume, 0);
                    sprintf(volume, "Current volume: %d\n", currentVolume);
                    SCI_WRITE(&sci0, volume);
                }
                break;
            case 'p': //Increase volume
                if(self->mode == 2) return;
                else if (self->mode == 1)
                    ASYNC(&musicPlayer, sendCanMsg, IncreaseVol);
                else {
                    currentVolume = SYNC(&toneGenerator, raiseVolume, 0);          
                    sprintf(volume, "Current volume: %d\n", currentVolume);
                    SCI_WRITE(&sci0, volume);
                }
                break;
            case 'm': //Mute
                if(self->mode == 2) return;
                else if (self->mode == 1)
                    ASYNC(&musicPlayer, sendCanMsg, ToggleMute);
                else{
                    ASYNC(&toneGenerator, mute, 0);
                    SCI_WRITE(&sci0, "Mute-toggle\n");
                }
                break;
            case 'a': //Play
                if(self->mode == 2) return;
                if (self->mode == 0 && !self->isPlaying){
                    self->currentMelodyIndex = 0;
                    self->stop = false;
                    ASYNC(&musicPlayer, playMelody, 0);
                    self->isPlaying = true;
                    SCI_WRITE(&sci0, "Play\n");
                }
                else if (self->mode == 1)
                    ASYNC(&musicPlayer, sendCanMsg, Play);
                break;
            case 's': //Stop
                if(self->mode == 2) return; 
                else if(self->mode == 1)
                    ASYNC(&musicPlayer, sendCanMsg, Stop);
                else{
                    self->isPlaying = false;
                    self->stop = true;
                    SYNC(&toneGenerator, enablePlay, 0);
                    SCI_WRITE(&sci0, "Stop\n");
                }
                break;
            case 'b': //Set tempo
                if(self->mode == 2) return;
                self->set_check = 1;
                SCI_WRITE(&sci0, "New tempo: ");
                break;
            case 'v': //Set key
                if(self->mode == 2) return;
                self->set_check = 2;
                SCI_WRITE(&sci0, "New key: ");
                break;
            case 'e': //Parse input
                if(self->mode == 2) return;
                if(self->set_check == 1){ // Set tempo
                    self->buff[self->buff_index++] = '\0';
                    int newTempo = atoi(self->buff);
                    if (newTempo >= 60 && newTempo <= 240){
                        self->tempo = newTempo;
                        if(self->mode == 1)
                            ASYNC(&musicPlayer, sendCanMsg, Tempo);
                    } else {
                        SCI_WRITE(&sci0, "\nTempo must be between 60 and 240!");
                    }
                }
                else if(self->set_check == 2){ // Set key
                    self->buff[self->buff_index++] = '\0';
                    int newKey = atoi(self->buff);
                    if (newKey <= 5 && newKey >= -5){
                        self->key = newKey;
                        if(self->mode == 1)
                            ASYNC(&musicPlayer, sendCanMsg, Key);

                    } else {
                        SCI_WRITE(&sci0, "\nKey must be between -5 and 5");
                    }
                }
                memset(self->buff, 0, sizeof self->buff);
                self->buff_index = 0;
                self->set_check = 0;
                SCI_WRITECHAR(&sci0, '\n');
                break;
            case 'g': // Toggle conductor
                self->mode = self->mode == 0 ? 1 : 0;
                if(self->mode == 0){
                    SCI_WRITE(&sci0, "Toggled normal mode\n");
                }
                else{
                    SCI_WRITE(&sci0, "Toggled conductor mode\n");
                    self->isPlaying = false;
                    self->stop = true;
                    SYNC(&toneGenerator, enablePlay, 0);
                    ASYNC(&musicPlayer, sendCanMsg, InitPlay);
                }
                
                break;
            case 'h': // Toggle musician
                self->mode = self->mode == 0 ? 2 : 0;
                if(self->mode == 0)
                    SCI_WRITE(&sci0, "Toggled normal mode\n");
                else{
                    self->isPlaying = false;
                    self->stop = true;
                    SYNC(&toneGenerator, enablePlay, 0);
                    SCI_WRITE(&sci0, "Toggled musician mode\n");
                }
                break;
                
        }
}

//typedef enum {Init, LowerVol, IncreaseVol, ToggleMute, Play, Stop, Tempo, Key} msgTypes;

void sendCanMsg(MusicPlayer *self, int value){
    CANMsg sendMsg;

    switch((enum msgTypes)value){
        case InitPlay:
            sendMsg.msgId = 1;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case LowerVol:
            sendMsg.msgId = 2;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case IncreaseVol:
            sendMsg.msgId = 3;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case ToggleMute:
            sendMsg.msgId = 4;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case Play:
            sendMsg.msgId = 5;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case Stop:
            sendMsg.msgId = 6;
            sendMsg.nodeId = 1;
            sendMsg.length = 0;
            break;
        case Tempo:
            sendMsg.msgId = 7;
            sendMsg.nodeId = 1;
            sendMsg.length = 1;
            sendMsg.buff[0] = (char)self->tempo;
            break;
        case Key:
            sendMsg.msgId = 8;
            sendMsg.nodeId = 1;
            sendMsg.length = 1;
            sendMsg.buff[0] = (char)self->key;
            break;
    }

    CAN_SEND(&can0, &sendMsg);
}

void receiver(MusicPlayer *self, int unused) {
    //if (self->mode != 2) return;
    CANMsg msg;
    CAN_RECEIVE(&can0, &msg);
    int currentVolume;
    char volume[24];
    enum msgTypes d = (enum msgTypes)msg.msgId;
    switch(d){
        case InitPlay:
            if(!self->isPlaying){
                self->currentMelodyIndex = 0;
                self->stop = false;
                ASYNC(&musicPlayer, playMelody, 0);
            }
            self->isPlaying = true;
            break;
        case LowerVol:
            currentVolume = SYNC(&toneGenerator, lowerVolume, 0);
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0, volume);
            break;
        case IncreaseVol:
            currentVolume = SYNC(&toneGenerator, raiseVolume, 0);          
            sprintf(volume, "Current volume: %d\n", currentVolume);
            SCI_WRITE(&sci0, volume);
            break;
        case ToggleMute:
            ASYNC(&toneGenerator, mute, 0);
            SCI_WRITE(&sci0, "Mute-toggle\n");
            break;
        case Play:
            if(!self->isPlaying){
                self->currentMelodyIndex = 0;
                self->stop = false;
                ASYNC(&musicPlayer, playMelody, 0);
            }
            self->isPlaying = true;
            SCI_WRITE(&sci0, "Play\n");
            break;
        case Stop:
            self->isPlaying = false;
            self->stop = true;
            SYNC(&toneGenerator, enablePlay, 0);
            SCI_WRITE(&sci0, "Stop\n");
            break;
        case Tempo:
            self->tempo = (int)msg.buff[0];
            SCI_WRITE(&sci0, "Tempo set\n");
            break;
        case Key:
            self->key = (int)msg.buff[0];
            SCI_WRITE(&sci0, "Key set\n");
            break;
    }
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
    INSTALL(&can0, can_interrupt, CAN_IRQ0);

    TINYTIMBER(&musicPlayer, startApp, 0);
    return 0;
}

void startApp(MusicPlayer *self, int arg) {

    SCI_INIT(&sci0);
    CAN_INIT(&can0);
    SCI_WRITE(&sci0, "Application loaded\n");
}
