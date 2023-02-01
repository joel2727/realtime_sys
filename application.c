#include "TinyTimber.h"
#include "sciTinyTimber.h"
#include "canTinyTimber.h"
#include <stdlib.h>
#include <stdio.h>
#include  <stdbool.h>

typedef struct {
    Object super;
    int count;
    char history_count;
    char buff[15];
    char c;
    int history[3];
    bool loop;
} App;

const int melody[32] = {0, 2, 4, 0, 0, 2, 4, 0, 4, 5, 7, 4, 5, 7, 7, 9, 7, 5, 4, 0, 7, 9, 7, 5, 4, 0, 0, -5, 0, 0, -5, 0};

const int period[25] = {
2024,
1911,
1803,
1702,
1607,
1516,
1431,
1351,
1275,
1203,
1136,
1072,
1012,
955,
901,
851,
803,
758,
715,
675,
637,
601,
568,
536,
506,
};

App app = { initObject(), 0, 0, {}, '\0', {}, false};

void num_history(App*, int);
void reader(App*, int);
void receiver(App*, int);

Serial sci0 = initSerial(SCI_PORT0, &app, num_history);

Can can0 = initCan(CAN_PORT0, &app, receiver);

void receiver(App *self, int unused) {
    CANMsg msg;
    CAN_RECEIVE(&can0, &msg);
    SCI_WRITE(&sci0, "Can msg received: ");
    SCI_WRITE(&sci0, msg.buff);
}

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
                char tmp_ptr[15];
                snprintf(tmp_ptr, 15, "Value typed: %d", num);
                self->buff[0] = '\0';
                self->count = 0;
                SCI_WRITE(&sci0, "\nRcv: \'");
                SCI_WRITE(&sci0, tmp_ptr);
                SCI_WRITE(&sci0, "\'\n");
                break;
        }
}

void num_history(App *self, int c){
    SCI_WRITE(&sci0, "Rcv: \'");
    SCI_WRITECHAR(&sci0, c);
    SCI_WRITE(&sci0, "\'\n");
    
    switch((char) c){
        case '0' ... '9':
            case '-':
                self->buff[self->count++] = (char)c;
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
        case 'F': 
            memset(self->history, 0, sizeof self->history);
            self->history_count = 0;
            self->loop = false;
            SCI_WRITE(&sci0, "3-History has been erased\n");
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

// TODO SCI_WRITE
void print_melody_transpose(int key) {
    if (key>5 || key<-5) {
        //printf("Unvalid key transpose\n");
    }
    for (int i = 0; i < 32; i++) {
        //printf("%d ", melody[i]+key);
    }
    //printf("\n");
    return;
}

void startApp(App *self, int arg) {
    CANMsg msg;

    CAN_INIT(&can0);
    SCI_INIT(&sci0);
    SCI_WRITE(&sci0, "Hello, hello...\n");

    msg.msgId = 1;
    msg.nodeId = 1;
    msg.length = 6;
    msg.buff[0] = 'H';
    msg.buff[1] = 'e';
    msg.buff[2] = 'l';
    msg.buff[3] = 'l';
    msg.buff[4] = 'o';
    msg.buff[5] = 0;
    CAN_SEND(&can0, &msg);
}

int main() {
    INSTALL(&sci0, sci_interrupt, SCI_IRQ0);
	INSTALL(&can0, can_interrupt, CAN_IRQ0);
    TINYTIMBER(&app, startApp, 0);
    return 0;
}
