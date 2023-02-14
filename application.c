/* USER GUIDE

Volume control:
'o' - lower volume
'p' - raise volume
'm' - toggle mute

Workload control:
'q' - decrease workload
'w' - increase workload

Deadline:
'd' - deadline toggle

*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include  <stdbool.h>
#include "constants.h"

#define TONE_DEADLINE 100
#define WORK_DEADLINE 1300
#define MAX_VOLUME 45


typedef struct {
    Object super;
    char c;
} App;
typedef struct {
    Object super;

    int period;
    int volume;
    bool high;
    bool mute;

    bool deadlineEnabled;
} Tone;

typedef struct {
    Object super;
    int background_loop_range;
    int load_period;
    bool deadlineEnabled;
} Work;


App app = { initObject(), '\0'};
Tone tone = { initObject(), 931, 1, false, false, false};
Work work = { initObject(), 1000, 1000, false};

//Pointer to DAC
int * volatile const addr_DAC = (int*)0x4000741C;


// Function Declarations
void reader(App*, int);
void receiver(App*, int);
void controller(App*, int);

void playTone(Tone*, int);
int lowerVolume(Tone*, int);
int raiseVolume(Tone*, int);
void mute(Tone*, int);
void toggleToneDeadline(Tone*, int);

void toggleWorkDeadline(Work*, int);
void setWork(Work*, int);

// Function Definitions

void playTone(Tone* self, int not_used) {

    self->high = !self->high;

    if (self->high && !self->mute) {
        *addr_DAC = self->volume;
        return;
    }

    *addr_DAC = 0;

    if (self->deadlineEnabled)
        SEND(USEC(self->period), TONE_DEADLINE, self, playTone, 0);
    else
        AFTER(USEC(self->period), self, playTone, 0);
}

int lowerVolume(Tone* self, int not_used) {
    if (self->volume > 0)
        self->volume--;
    return self->volume;
}

int raiseVolume(Tone* self, int not_used) {
    if (self->volume < MAX_VOLUME)
        self->volume++;
    return self->volume;
}

void mute(Tone* self, int not_used) {
    self->mute = !self->mute;
}

void setWork(Work* self, int not_used) {
    for (int i = 0; i < self->background_loop_range; i++);
    if (self->deadlineEnabled)
        SEND(USEC(self->load_period), WORK_DEADLINE, self, setWork, 0);
    else  
        AFTER(USEC(self->load_period), self, setWork, 0);
}

int increaseWorkload(Work* self, int not_used) {
    if (self->background_loop_range > 0)
    self->background_loop_range -= 500;

    return self->background_loop_range;
}

void toggleWorkDeadline(Work* self, int not_used) {
    self->deadlineEnabled = !self->deadlineEnabled;
}

void toggleToneDeadline(Tone* self, int not_used) {
    self->deadlineEnabled = !self->deadlineEnabled;
}

int lowerWorkload(Work* self, int not_used) {
    self->background_loop_range += 500;

    return self->background_loop_range;
}

// Communication
Serial sci0 = initSerial(SCI_PORT0, &app, controller);

void controller(App *self, int c){
    int currentVolume;
    char volume[20];
    char work_loop_value[44];
    int background_loop_range;

    switch ((char)c) {
            case 'o': //Lower volume
                currentVolume = SYNC(&tone, lowerVolume, 0);

                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);
                break;
            case 'p': //Increase volume
                currentVolume = SYNC(&tone, raiseVolume, 0);          
                
                sprintf(volume, "Current volume: %d\n", currentVolume);
                SCI_WRITE(&sci0, volume);
                break;
            case 'm': //Mute
                mute(&tone, 0);
                SCI_WRITE(&sci0, "Mute-toggle");
                break;
            case 'q': //Lower workload
                background_loop_range = SYNC(&work, lowerWorkload, 0);
                
                snprintf(work_loop_value, 33, "Workload: %d\n", background_loop_range);
                SCI_WRITE(&sci0, work_loop_value);
                break;
            case 'w': //Increase workload
                background_loop_range = SYNC(&work, increaseWorkload, 0);
                
                snprintf(work_loop_value, 33, "Workload: %d\n", background_loop_range);
                SCI_WRITE(&sci0, work_loop_value);
                break;
            case 'd':
                
                SYNC(&work, toggleWorkDeadline, 0);
                SYNC(&tone, toggleToneDeadline, 0);
                
                SCI_WRITE(&sci0, "Toggle deadline!\n");
                break;
        }
} 

void startApp(App *self, int arg) {

    SCI_INIT(&sci0);
    SCI_WRITE(&sci0, "Hello, hello...\n");

    ASYNC(&tone, playTone, 0);
    ASYNC(&work, setWork, 0);
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);

    TINYTIMBER(&tone, startApp, 0);
    return 0;
}
