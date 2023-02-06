/* USER GUIDE

To use the program, enter interger(s) and press 'e' to parse the input
and display the final value. Enter '-' at start of input to input a negative value. 
To clear the buffer, press 'F'. 


'e' - end integer input
'F' - Erase 3-history
*/


#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include  <stdbool.h>
#include "constants.h"

typedef struct {
    Object super;
    int count;
    char history_count;
    char buff[15];
    char c;
    int history[3];
    bool loop;
} App;

typedef struct {
    Object super;
    int period;
    int length;
    int volume;
    bool mute;
    //int load_period;
    //int background_loop_range;

} Tone;

typedef struct {
    Object super;
    int background_loop_range;
    int load_period;
} Work;

App app = { initObject(), 0, 0, {}, '\0', {}, false};
Tone tone = { initObject(), 931, 1, 3, false};
Work work = { initObject(), 1300, 1000};

void num_history(App*, int);
void reader(App*, int);
void receiver(App*, int);

void tone_control(Tone*, int);
void high(Tone*, int);
void low(Tone*, int);

//Setters/getters
int get_period(Work* self, int unused);
void set_period(Work* self, int period);
void set_loop_range(Work* self, int range);
int get_loop_range(Work* self, int unused);

void set_work(Work*, int);

int * volatile const addr_DAC = (int*)0x4000741C;

int get_period(Work* self, int unused){
    return self->load_period;
}
void set_period(Work* self, int period){
    self->load_period = period;
}
void set_loop_range(Work* self, int range){
    self->background_loop_range = range;
}
int get_loop_range(Work* self, int unused){
    return self->background_loop_range;
}



void high(Tone* self, int not_used) {
    if (!self->mute) {
        *addr_DAC = self->volume;
    }
    AFTER(USEC(self->period), self, low, 0);
}

void low(Tone* self, int not_used) {
    *addr_DAC = 0;
    AFTER(USEC(self->period), self, high, 0);
}

void set_work(Work* self, int not_used) {
    for (int i = 0; i < self->background_loop_range; i++) {

    }
    AFTER(USEC(self->load_period), self, set_work, 0);
}

// Util functions

// Returns index of median in list of 3 int
int calculate_median(int[3]);

// Communication
Serial sci0 = initSerial(SCI_PORT0, &app, num_history);

void reader(App *self, int c) {
        switch ((char)c) {
            case '0'...'9':
                case '-':
                    self->buff[self->count++] = (char)c;
                    SCI_WRITECHAR(&sci0, c);
                    break;
            case 'e':
                self->buff[self->count++] = '\0';
                int num = atoi(self->buff);
                SCI_WRITE(&sci0, "\n");
                print_melody_transpose(self, num);
                memset(self->buff, 0, sizeof self->buff);//self->buff[0] = '\0';
                self->count = 0;
                /*char tmp_ptr[15];
                snprintf(tmp_ptr, 15, "Value typed: %d", num);
                self->buff[0] = '\0';
                self->count = 0;
                SCI_WRITE(&sci0, "\nRcv: \'");
                SCI_WRITE(&sci0, tmp_ptr);
                SCI_WRITE(&sci0, "\'\n");*/
                break;
        }
}

void tone_control(Tone *self, int c){
    char volume[3];
    char work[33];
    switch ((char)c) {
            case 'o':
                if (self->volume > 0) {
                    self->volume--;
                }

                sprintf(&volume, "%d", self->volume);
                
                SCI_WRITE(&sci0, volume);
                break;
            case 'p':
                if (self->volume < 25) {
                    self->volume++;
                }
                
                
                sprintf(&volume, "%d", self->volume);
                
                SCI_WRITE(&sci0, volume);
                break;
            case 'm':
                self->mute = !self->mute;
                
                SCI_WRITE(&sci0, "Mute-toggle");
                break;
            case 'q':

                int background_loop_range = SYNC(&work, get_loop_range, 0);
                
                if (background_loop_range > 0)
                    SYNC(&work, set_loop_range, background_loop_range-500);
                snprintf(work, 33, "%d", background_loop_range-500);
                SCI_WRITE(&sci0, "Lower workload\n");
                SCI_WRITE(&sci0, work);
                SCI_WRITE(&sci0, "\n");
                break;
            case 'w':
                SYNC(&work, set_loop_range, background_loop_range+500);
                snprintf(work, 33, "%d", background_loop_range+500);
                SCI_WRITE(&sci0, "Lower workload\n");
                SCI_WRITE(&sci0, work);
                SCI_WRITE(&sci0, "\n");
                break;
        }
} 

void num_history(App *self, int c){
    
    switch((char) c){
        case '0' ... '9':
            case '-':
                self->buff[self->count++] = (char)c;

                SCI_WRITE(&sci0, "Rcv: \'");
                SCI_WRITECHAR(&sci0, c);
                SCI_WRITE(&sci0, "\'\n");
                break;
            case 'F': 
                SCI_WRITE(&sci0, "Rcv: \'");
                SCI_WRITECHAR(&sci0, c);
                SCI_WRITE(&sci0, "\'\n");
                memset(self->history, 0, sizeof self->history);
                self->history_count = 0;
                self->loop = false;
                SCI_WRITE(&sci0, "3-History has been erased\n");
                break;
        case 'e':
            int num = atoi(self->buff); //convert buff content to int
            self->history[self->history_count++] = num; //insert typed int in history array
            self->history_count = self->history_count % 3; //history pointer loop around
            if (self->history_count==0) self->loop = true;
            
            char tmp[64]; 
            self->buff[self->count++] = '\0'; //null-terminate

            //Find median
            int median;
            if (self->loop) {
                int index = calculate_median(self->history); // Calculate median
                median = self->history[index];
            } else {
                if (self->history_count == 1) median = self->history[0];
                if (self->history_count == 2) median = (self->history[0]+self->history[1])/2;
            }
            
            snprintf(tmp, 64, "Value typed: %d, sum: %d, median: %d\n", num, (self->history[0]+self->history[1]+self->history[2]), median);
            SCI_WRITE(&sci0, tmp);

            memset(self->buff, 0, sizeof self->buff);
            self->count = 0;
            break;
    }
}

int calculate_median(int arr[3]) {
    if (arr[0] < arr[1]) {
        if (arr[1] < arr[2]) {
            return 1;
        } else if (arr[0] < arr[2]) {
            return 2;
        } else {
            return 0;
        }
    } else {
        if (arr[0] < arr[2]) {
            return 0;
        } else if (arr[1] < arr[2]) {
            return 2;
        } else {
            return 1;
        }
    }
}

void print_melody_transpose(App *self, int key) {
    if (key>5 || key<-5) {
        SCI_WRITE(&sci0, "Unvalid key transpose\n");
        return;
    }

    char key_string[3];
    snprintf(key_string, 3, "%d", key);
    SCI_WRITE(&sci0, "Key: ");
    SCI_WRITE(&sci0, key_string);
    SCI_WRITE(&sci0, "\n");

    char note[6];

    for (int i = 0; i < 32; i++) {
        snprintf(note, 6, "%d ", period[10+melody[i]+key]);
        SCI_WRITE(&sci0, note);
    }

    SCI_WRITE(&sci0, "\n");
    return;
}

void startApp(App *self, int arg) {

    SCI_INIT(&sci0);
    SCI_WRITE(&sci0, "Hello, hello...\n");

    //print_melody_transpose(&app, -1);

    //high(&tone, 0);
    //set_work(&tone, 0);
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);

    TINYTIMBER(&app, startApp, 0);
    return 0;
}
