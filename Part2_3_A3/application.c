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

Conductor/Musician 

To be in musician:
    * Comment out constant CONDUCTOR before compiling
To be in conductor mode:
    * Uncomment CONDUCTOR constant before compiling
*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "constants.h"
#include <time.h>
#include <string.h>

#define TONE_DEADLINE 100
#define MAX_VOLUME 45

#define CONDUCTOR 1 //Comment this to ready the board for musician mode

enum msgTypes {LowerVol=2, IncreaseVol=3, ToggleMute=4, Play=5, Stop=6, Tempo=7, Key=8};

enum msgTypes can_type;


typedef struct {
    Object super;
    char c;
    int currentMelodyIndex;
    int tempo;
    int key;
    char buff[15];
    int buff_index;
    bool isPlaying;
    bool isReady;
    int volume;
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

MusicPlayer musicPlayer = { initObject(), ' ', 0, 120, 0, {}, 0, false, true, 1, 0};
ToneGenerator toneGenerator = { initObject(), 500, 1, false, false, false};

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;


// Function Declarations
void reader(MusicPlayer*, int);
void receiver(MusicPlayer*, int);
void loopReceiver(MusicPlayer*, int);
void controller(MusicPlayer*, int);
void loopController(MusicPlayer*, int);
void startApp(MusicPlayer*, int);

void start(ToneGenerator*, int);
void toggleStop(ToneGenerator*, int);
void enablePlay(ToneGenerator*, int);

int lowerVolume(ToneGenerator*, int);
int raiseVolume(ToneGenerator*, int);
void mute(ToneGenerator*, int);
void setPeriod(ToneGenerator*, int);

// Function Definitions

#ifdef CONDUCTOR

///CONDUCTOR
// Communication 
Serial sci0 = initSerial(SCI_PORT0, &musicPlayer, controller);

// CAN Communication
Can can0 = initCan(CAN_PORT0, &musicPlayer, loopReceiver);

#endif

#ifndef CONDUCTOR

/// MUSICIAN
// Communication 
Serial sci0 = initSerial(SCI_PORT0, &musicPlayer, loopController);

// CAN Communication
Can can0 = initCan(CAN_PORT0, &musicPlayer, receiver);
#endif


void playMelody(MusicPlayer* self, int unused){
    if (!self->isPlaying) {
        self->isReady = true;
        return;
    };

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

void controller(MusicPlayer *self, int c){ // Conductor
    int currentVolume;
    char volume[24];
    
    CANMsg sendMsg;
    sendMsg.length = 0;
    sendMsg.nodeId = 1;

    switch ((char)c) {
            case '0'...'9':
                case '-':
                    SCI_WRITECHAR(&sci0, (char)c);
                    self->buff[self->buff_index++] = (char)c;
                    return;
            case 'o': //Lower volume
                sendMsg.msgId = LowerVol;
                currentVolume = SYNC(&toneGenerator, lowerVolume, 0);
                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);                
                break;
            case 'p': //Increase volume
                sendMsg.msgId = IncreaseVol;
                currentVolume = SYNC(&toneGenerator, raiseVolume, 0);          
                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);
                break;
            case 'm': //Mute
                sendMsg.msgId = ToggleMute;
                ASYNC(&toneGenerator, mute, 0);
                SCI_WRITE(&sci0, "Mute-toggle\n");
                break;
            case 'a': //Play
                if (self->isPlaying) return;
                if (!self->isReady) return;
                sendMsg.msgId = Play;
                self->currentMelodyIndex = 0;
                self->isPlaying = true;
                ASYNC(&musicPlayer, playMelody, 0);
                SCI_WRITE(&sci0, "Play\n");
                break;
            case 's': //Stop
                sendMsg.msgId = Stop; 
                if (!self->isPlaying) return;
                self->isPlaying = false;
                self->isReady = false;
                SYNC(&toneGenerator, toggleStop, 0);
                SCI_WRITE(&sci0, "Stop\n");            
                break;
            case 'b': //Set tempo
                self->set_check = 1;
                SCI_WRITE(&sci0, "New tempo: ");
                return;
            case 'v': //Set key
                self->set_check = 2;
                SCI_WRITE(&sci0, "New key: ");
                return;
            case 'e': //Parse input
                bool clearReturn = false;

                if(self->set_check == 1){ // Set tempo
                    self->buff[self->buff_index++] = '\0';
                    int newTempo = atoi(self->buff);
                    if (newTempo >= 60 && newTempo <= 240){
                        self->tempo = newTempo;
                        sendMsg.msgId = Tempo; 
                    } else {
                        SCI_WRITE(&sci0, "\nTempo must be between 60 and 240!");
                        clearReturn = true;
                    }
                }
                else if(self->set_check == 2){ // Set key
                    self->buff[self->buff_index++] = '\0';
                    int newKey = atoi(self->buff);
                    if (newKey <= 5 && newKey >= -5){
                        self->key = newKey;
                        sendMsg.msgId = Key;
                    } else {
                        SCI_WRITE(&sci0, "\nKey must be between -5 and 5");
                        clearReturn = true;
                    }
                }

                // Copy buffer
                if (self->set_check != 0 && !clearReturn) {
                    sendMsg.length = self->buff_index;
                    strcpy((char*)sendMsg.buff, self->buff);
                }

                // Always clear buffer
                memset(self->buff, 0, sizeof self->buff);
                self->buff_index = 0;
                self->set_check = 0;
                SCI_WRITECHAR(&sci0, '\n');
                if (clearReturn) return;
                break;
            default:
                return;
        }
    CAN_SEND(&can0, &sendMsg);
}

void loopController(MusicPlayer *self, int c){
    CANMsg sendMsg;

    sendMsg.length = 0;
    sendMsg.nodeId = 1;

    switch ((char)c) {
            case '0'...'9':
                case '-':
                    SCI_WRITECHAR(&sci0, (char)c);
                    self->buff[self->buff_index++] = (char)c;
                    return;
            case 'o': //Lower volume
                sendMsg.msgId = LowerVol;             
                break;
            case 'p': //Increase volume
                sendMsg.msgId = IncreaseVol;
                break;
            case 'm': //Mute
                sendMsg.msgId = ToggleMute;
                break;
            case 'a': //Play
                if (self->isPlaying) return;
                if (!self->isReady) return;
                sendMsg.msgId = Play;
                break;
            case 's': //Stop
                if (!self->isPlaying) return;
                sendMsg.msgId = Stop;        
                break;
            case 'b': //Set tempo
                self->set_check = 1;
                SCI_WRITE(&sci0, "New tempo: ");
                return;
            case 'v': //Set key
                self->set_check = 2;
                SCI_WRITE(&sci0, "New key: ");
                return;
            case 'e': //Parse input
                bool clearReturn = false;

                if(self->set_check == 1){ // Set tempo
                    self->buff[self->buff_index++] = '\0';
                    int newTempo = atoi(self->buff);
                    if (newTempo >= 60 && newTempo <= 240){
                        sendMsg.msgId = Tempo; 
                    } else {
                        SCI_WRITE(&sci0, "\nTempo must be between 60 and 240!");
                        clearReturn = true;
                    }
                }
                else if(self->set_check == 2){ // Set key
                    self->buff[self->buff_index++] = '\0';
                    int newKey = atoi(self->buff);
                    if (newKey <= 5 && newKey >= -5){
                        sendMsg.msgId = Key;
                    } else {
                        SCI_WRITE(&sci0, "\nKey must be between -5 and 5");
                        clearReturn = true;
                    }
                }

                // Copy buffer
                if (self->set_check != 0 && !clearReturn) {
                    sendMsg.length = self->buff_index;
                    strcpy((char*)sendMsg.buff, self->buff);
                }

                // Always clear buffer
                memset(self->buff, 0, sizeof self->buff);
                self->buff_index = 0;
                self->set_check = 0;
                SCI_WRITECHAR(&sci0, '\n');
                if (clearReturn) return;
                break;
            default:
                return;
        }
    CAN_SEND(&can0, &sendMsg);
}

void loopReceiver(MusicPlayer *self, int unused) {
    CANMsg msg;
    CAN_RECEIVE(&can0, &msg);
    char volume[40];
    enum msgTypes d = (enum msgTypes)msg.msgId;
    switch(d){
        case LowerVol:
            SCI_WRITE(&sci0, "Lower volume msg received!\n");
            break;
        case IncreaseVol:
            SCI_WRITE(&sci0, "Increase volume msg received!\n");
            break;
        case ToggleMute:
            SCI_WRITE(&sci0, "Mute msg received!\n");
            break;
        case Play:
            SCI_WRITE(&sci0, "Play msg received!\n");
            break;
        case Stop:
            SCI_WRITE(&sci0, "Stop msg received!\n");
            break;
        case Tempo:
            sprintf(volume, "Set tempo msg received! Value: %d\n", atoi((char*)msg.buff));
            SCI_WRITE(&sci0, volume);
            break;
        case Key:
            sprintf(volume, "Set key msg received! Value: %d\n", atoi((char*)msg.buff));
            SCI_WRITE(&sci0, volume);
            break;
    }
}

void receiver(MusicPlayer *self, int unused) {
    CANMsg msg;
    CAN_RECEIVE(&can0, &msg);

    int currentVolume;
    char volume[40];

    enum msgTypes d = (enum msgTypes)msg.msgId;

    switch(d){
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
            if (self->isPlaying) return;
            if (!self->isReady) return;
            self->currentMelodyIndex = 0;
            self->isPlaying = true;
            ASYNC(&musicPlayer, playMelody, 0);
            SCI_WRITE(&sci0, "Play\n");
            break;
        case Stop:
            if (!self->isPlaying) return;
            self->isReady = false;
            self->isPlaying = false;
            SYNC(&toneGenerator, toggleStop, 0);
            SCI_WRITE(&sci0, "Stop\n");            
            break;
        case Tempo:
            self->tempo = atoi((char*)msg.buff);
            SCI_WRITE(&sci0, "Tempo set\n");
            break;
        case Key:
            self->key = atoi((char*)msg.buff);          
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
