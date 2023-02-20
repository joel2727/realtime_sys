/* USER GUIDE

Uncomment BENCHMARK constant for benchmark testing

Volume control:
'o' - lower volume
'p' - raise volume
'm' - toggle mute

Workload control:
'q' - decrease workload
'w' - increase workload

Deadline:
'd' - deadline toggle

Pitch drop occurs at loop_range = 14000

WCET for setWork (loop range = 14000)
Results - Max: 1340 us, Avg: 1330 us

WCET for setWork (loop range = 1000)
Results - Max: 100 us, Avg: 100 us

WCET for playTone
Results - Max: 0.230 us, Avg: 0.230 us

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

//#define BENCHMARK 1 //Uncomment for benchmarking


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

App app = { initObject(), ' '};
Tone tone = { initObject(), 500, 1, false, false, false};
Work work = { initObject(), 1000, 1300, false};

//Pointer declarations
volatile unsigned int * addr_dac = (volatile unsigned int * )0x4000741C;

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

long average(long*);

// Function Definitions

// Communication 
Serial sci0 = initSerial(SCI_PORT0, &app, controller);

void playTone(Tone* self, int not_used) {
    self->high = !self->high;

    if (self->high && !self->mute) 
        *addr_dac = (self->volume);
    else
        *addr_dac = 0;

    #ifndef BENCHMARK
    if (self->deadlineEnabled)
        SEND(USEC(self->period), TONE_DEADLINE, self, playTone, 0);
    else
        AFTER(USEC(self->period), self, playTone, 0);
    #endif
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

void setPeriod(Tone* self, int period) {
    self->period = period;
}

void setWork(Work* self, int not_used) {
    for (int i = 0; i < self->background_loop_range; i++);
    
    #ifndef BENCHMARK
    if (self->deadlineEnabled)
        SEND(USEC(self->load_period), WORK_DEADLINE, self, setWork, 0);
    else  
        AFTER(USEC(self->load_period), self, setWork, 0);
    #endif
}

int lowerWorkload(Work* self, int not_used) {
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

int increaseWorkload(Work* self, int not_used) {
    self->background_loop_range += 500;

    return self->background_loop_range;
}

void controller(App *self, int c){
    int currentVolume;
    char volume[24];
    char work_loop_value[18];
    int background_loop_value;

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
                background_loop_value = SYNC(&work, lowerWorkload, 0);
                
                snprintf(work_loop_value, 18, "Workload: %d\n", background_loop_value);
                SCI_WRITE(&sci0, work_loop_value);
                break;
            case 'w': //Increase workload
                background_loop_value = SYNC(&work, increaseWorkload, 0);
                
                snprintf(work_loop_value, 18, "Workload: %d\n", background_loop_value);
                SCI_WRITE(&sci0, work_loop_value);
                break;
            case 'd':
                
                ASYNC(&work, toggleWorkDeadline, 0);
                ASYNC(&tone, toggleToneDeadline, 0);
                
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

volatile void runTest(App *self, int arg) {
    SCI_INIT(&sci0);

    long diff;
    long time_arr[500];
    char results[48];
    long max = 0;
    Time before;
    Time after;

    for (int i = 0; i < 500; i++) {

        before = CURRENT_OFFSET();
        playTone(&tone, 0);
        after = CURRENT_OFFSET();
        diff = USEC_OF(after - before);

        time_arr[i] = diff;
        if (diff > max)
            max = diff;
    }

    long avg = average(time_arr);

    snprintf(results, 48, "Results - Max: %ld, Avg: %ld\n", max, avg);
    SCI_WRITE(&sci0, results);

}

long average(long *arr){
    long sum = 0;
    size_t arr_size = sizeof(arr) / sizeof(arr[0]);
    for(int i = 0; i < arr_size; i++){
        sum += arr[i];
    }
    return sum/arr_size;
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);

    TINYTIMBER(&app, startApp, 0);
    return 0;
}
