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

// Pitch drop occurs at loop_range = 10500

*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include  <stdbool.h>
#include "constants.h"
#include <time.h>
#include "stm32f4xx.h"

#define TONE_DEADLINE 100
#define WORK_DEADLINE 1300
#define MAX_VOLUME 45

#define BENCHMARK 1 //Uncomment for benchmarking


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

double average(double*);

// Function Definitions

// Communication 
Serial sci0 = initSerial(SCI_PORT0, &app, controller);

void playTone(Tone* self, int not_used) {

    self->high = !self->high;

    if (self->high && !self->mute) 
        *addr_DAC = self->volume;
    else
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

void setPeriod(Tone* self, int period) {
    self->period = period;
}

void setWork(Work* self, int not_used) {
    for (int i = 0; i < self->background_loop_range; i++);
    
    #ifndef BENCHMARK
    SCI_WRITE(&sci0, "Fault here!\n");
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

void runTest(App *self, int arg) {
    SCI_INIT(&sci0);

    volatile uint32_t start;
    volatile uint32_t end;
    double time_arr[500];
    uint32_t max = 0;

    char results[48];

    //Timer timer = initTimer();


    for (int i = 0; i < 500; i++) {
        start = SysTick->VAL;
        setWork(&work, 0);
        end = SysTick->VAL;

        time_arr[i] = (double) (end - start) / CLOCKS_PER_SEC;

        if (time_arr[i] > max)
            max = time_arr[i];
    }

    double avg = average(time_arr);

    snprintf(results, 48, "Results - Max: %ld, Avg: %ld\n", start, end);
    SCI_WRITE(&sci0, results);

}

double average(double *arr){
    double sum = 0;
    size_t size = sizeof(arr) / sizeof(arr[0]);
    for(int i = 0; i < size; i++){
        sum += arr[i];
    }
    return (double)sum/size;
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);

    TINYTIMBER(&app, runTest, 0);
    return 0;
}
