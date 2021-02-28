#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "math.h"

#define UART_ID uart0
#define BAUD_RATE 9600
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define GATE_PIN 22

uint8_t numLanes = 8;
uint8_t numDec = 4;
uint8_t gpio[] = {11,12,13,14,15,16,17,18};
uint64_t laneTicks[8];
uint8_t laneFinishPosition[8];
uint64_t previousTicks;
uint16_t positionAllowance = 1;
uint8_t char_rxed = 0;
uint8_t positionMarker[4]={'a','A','1','!'};
uint8_t positionIndex = 0;
uint8_t laneMarker[4] = {'A','1','a','A'};
uint8_t laneIndex = 0;
int32_t alarmID;
uint8_t resetTime = 0; // seconds between automatic reset between races

char msg[50];
char * msgptr=msg;

bool racing = false;
bool masking = false;
bool laneEnabled[8];
bool laneMasked[8];

struct lanestruct {
    uint32_t laneticks;
    uint8_t laneindex;
};

struct lanestruct raceData[8];

int laneticksSort(const void *a, const void *b) {
    struct lanestruct *a1 = (struct lanestruct *)a;
    struct lanestruct *a2 = (struct lanestruct *)b;
    if ((*a1).laneticks < (*a2).laneticks)
        return -1;
    else if ((*a1).laneticks > (*a2).laneticks)
        return 1;
    else
        return 0;
}

int laneindexSort(const void *a, const void *b) {
    struct lanestruct *a1 = (struct lanestruct *)a;
    struct lanestruct *a2 = (struct lanestruct *)b;
    if ((*a1).laneindex < (*a2).laneindex)
        return -1;
    else if ((*a1).laneindex > (*a2).laneindex)
        return 1;
    else
        return 0;
}

void process_finish_order();
void print_finish_order();
void process_command(const char* message);


void lane_enable(uint8_t laneindex, bool active) {
    gpio_set_irq_enabled(gpio[laneindex], GPIO_IRQ_EDGE_FALL, active);
    laneEnabled[laneindex] = active;
}

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    // do stuff to end the incomplete race
    uart_puts(UART_ID, "Race did not complete\r\n");
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, false);
    }
    return 0;
}

void reset(void) {
    gpio_set_irq_enabled(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    racing = false;
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, false);
    }
    uart_puts(UART_ID,"OK\r\n");
}

void start_race() {
    previousTicks = time_us_64 ();
    alarmID = add_alarm_in_ms(10000, alarm_callback, NULL, false);
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, !laneMasked[i]);
        laneTicks[i] = previousTicks + 9999999;
    }
    uart_puts(UART_ID, "They're off!\r\n");
    gpio_set_irq_enabled(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
    racing = true;
}

void on_uart_rx() {
    while (uart_is_readable(UART_ID)) {
        uint8_t ch = uart_getc(UART_ID);
        if (ch == 13) {
            msg[char_rxed] = 0; // null terminate the string
            process_command(msg);
            char_rxed = 0;
            for (uint8_t i = 0; i < 50; i++) msg[i] = 0;
        }
        else {
            msg[char_rxed] = ch;
            char_rxed++;
        }
    }
}

void process_command(const char* message) {
    switch (toupper(message[0])) {
        case 'A': // set allowance for finish position
            if (message[1] == 0) {
                char convertido[6];
                sprintf(convertido, "%u", positionAllowance);
                uart_puts(UART_ID, convertido);
                uart_puts(UART_ID, "\r\n");
            }
            else {
                char substr[20];
                strcpy(substr, &(message[1]));
                positionAllowance = atoi(substr);
                uart_puts(UART_ID,"OK\r\n");
            }
            break;
        case 'R':
            switch (toupper(message[1])) {
                case 'A': // Force end of race, return results, then reset
                    racing = false;
                    for (uint8_t i = 0; i < numLanes; i++) {
                        laneEnabled[i] = false;
                        lane_enable(i, false);
                    }
                    print_finish_order();
                    break;
                case 'G': // return results at end of race (we do that anyway, so just acknowledge)
                    uart_puts(UART_ID, "\r\n");
                    break;
                case 'P': // Return reesults from previous race
                    print_finish_order();
                    break;
                case 'R': // Read reset switch - active if gate is closed and ready for race
                    uart_putc(UART_ID, (int)(gpio_get(GATE_PIN)) + '0');
                    uart_puts(UART_ID, "\r\n");
                    break;
                case 'S': // Read start switch - active is gate is open and racing
                    uart_putc(UART_ID, (int)(!gpio_get(GATE_PIN)) + '0');
                    uart_puts(UART_ID, "\r\n");
                    break;
                case 'L': // Read lane switches
                    for (uint8_t i = 0; i < numLanes; i++) {
                        if (!laneMasked[i]) uart_putc(UART_ID, (int)(gpio_get(gpio[i])) + '0');
                        else uart_putc(UART_ID,'-');
                    }
                    uart_puts(UART_ID, "\r\n");
                    break;
                case '\0': // Reset
                    reset();
                    break;
                default: 
                    break;
            }
            break;
        case 'O':
            switch (toupper(message[1])) {
                case 'D': // Set / Read number of decimal places in the result values
                    if (message[2] == 0) {  // read the number of decimal places
                        uart_putc(UART_ID, numDec + '0');
                        uart_puts(UART_ID, "\r\n");
                    }
                    else if ((message[2] - '0' > 2) && (message[2] - '0' < 6)) { // set the number of decimal places between 3 - 5
                        numDec = message[2] - '0';
                        uart_puts(UART_ID, "OK\r\n");
                    }
                    else uart_puts(UART_ID, "?\r\n");                
                    break;
                case 'F': // set/read photo finish trigger delay -- GPRM sends 'of24'
                    uart_puts(UART_ID, "OK\r\n");
                    break;

                case 'L': // Set / Read lane character
                    if (message[2] == 0) {  // return the current marker
                        uart_putc(UART_ID, laneMarker[laneIndex]);
                        uart_puts(UART_ID, "\r\n");
                    }
                    else if ((message[2] - '0' >= 0) && (message[2] - '0' < 4)) { // set the lane marker
                        laneIndex = message[2] - '0';
                        uart_puts(UART_ID, "OK\r\n");
                    }
                    else uart_puts(UART_ID, "?\r\n");
                    break;
                case 'M': // Set Read lane mask
                    if (message[2] == 0) { // show the masked lanes
                        for (uint8_t i = 0; i < numLanes; i++ ) {
                            if (laneMasked[i]) uart_putc(UART_ID, i + '1');
                        }
                        uart_puts(UART_ID,"\r\n");
                    }
                    else { 
                        uint8_t chardec = message[2] - '0';
                        if (chardec > 0 && chardec < numLanes + 1) {
                            laneMasked[chardec - 1] = true;
                            uart_puts(UART_ID,"OK\r\n");
                        }
                        else if (chardec == 0) {
                            for (uint8_t i = 0; i < numLanes; i++ ) {
                                laneMasked[i] = false;
                            }
                            uart_puts(UART_ID,"OK\r\n");
                        }
                        else uart_puts(UART_ID,"?\r\n");
                    }
                    break;
                case 'N': // Set / Read number of lanes
                    if (message[2] == 0) {  // return the number of lanes
                        uart_putc(UART_ID, numLanes + '0');
                        uart_puts(UART_ID, "\r\n");
                    }
                    else if ((message[2] - '0' > 0) && (message[2] - '0' < 9)) { // set the number of lanes
                        numLanes = message[2] - '0';
                        uart_puts(UART_ID, "OK\r\n");
                    }
                    else uart_puts(UART_ID, "?\r\n");

                    break;
                case 'P': // Set / Read place character
                    if (message[2] == 0) {  // return the current marker
                        uart_putc(UART_ID, positionMarker[positionIndex]);
                        uart_puts(UART_ID, "\r\n");
                    }
                    else if ((message[2] - '0' >= 0) && (message[2] - '0' < 4)) { // set the position marker
                        positionIndex = message[2] - '0';
                        uart_puts(UART_ID, "OK\r\n");
                    }
                    else uart_puts(UART_ID, "?\r\n");


                    break;
                case 'R': // Set / Read automatic reset delay (or/or0/orxx) - GPRM sends 'or0'
                    if (message[2] == 0) { // display the reset time
                        char convertido[6];
                        sprintf(convertido, "%u", resetTime);
                        uart_puts(UART_ID, convertido);
                        uart_puts(UART_ID, "\r\n");
                    }
                    else {
                        char substr[20];
                        strcpy(substr, &(message[2]));
                        resetTime = atoi(substr);
                        uart_puts(UART_ID,"OK\r\n");
                    }
                    break;
                case 'V': // Lane reversal (ov/ov1/ov0) - GPRM sends 'ov0'
                    uart_puts(UART_ID, "OK\r\n");
                    break;
                case 'W': // set / read photo finish trigger length - GPRM sends 'ow15'
                    uart_puts(UART_ID, "OK\r\n");
                    break;
                case 'X': // DTX000 mode (not supported)
                    uart_puts(UART_ID, "\r\n");
                    break;
                default:
                    uart_puts(UART_ID, "?\r\n");
                    break;
            }

            break;
        case 'V':
            uart_puts(UART_ID,"eTekGadget SmartLine Timer\r\n");
            break;
        default: // no idea what to do
            uart_puts(UART_ID, "?\r\n");
            break;
    }
    message = '\0';
}

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == GATE_PIN && events == 0x4) {
        start_race();
    }
    else {
        laneTicks[gpio - 11] = time_us_64 ();
        laneEnabled[gpio - 11] = false;
    }
}

void process_finish_order (void) {
    for (uint8_t i = 0; i < numLanes; i++) {
        raceData[i].laneticks = round((float)(laneTicks[i] - previousTicks) / 10);;
        raceData[i].laneindex = i;
    }

    qsort(raceData, numLanes, sizeof(raceData[0]), laneticksSort);

    laneFinishPosition[raceData[0].laneindex] = 1;
    for (int i = 1; i < numLanes; i++) {
        if (raceData[i].laneticks - raceData[i-1].laneticks <= positionAllowance) {
            laneFinishPosition[raceData[i].laneindex] = laneFinishPosition[raceData[i-1].laneindex];
        }
        else if (raceData[i].laneticks == 1000000) {
            laneFinishPosition[raceData[i].laneindex]=0;
        }
        else {
            laneFinishPosition[raceData[i].laneindex] = i + 1;
        }
    }
}

void print_finish_order (void) {
    process_finish_order();
    qsort(raceData, numLanes, sizeof(raceData[0]), laneindexSort);

    for (unsigned int i = 0; i < numLanes; i++) {
        uart_putc(UART_ID, (char)(laneMarker[laneIndex] + i));
        uart_putc(UART_ID, '=');

        float time = (float)raceData[i].laneticks / 100000;
        char convertido[16];
        sprintf(convertido, "%.5f", time);

        if (!laneMasked[i]) {
            uint8_t strLength;
            if (time >= 10) strLength = numDec + 3;
            else strLength = numDec + 2;

            for (uint8_t i = 0; i < strLength; i++) uart_putc(UART_ID, convertido[i]);
        }
        else {
            uart_puts(UART_ID, "10.");
            for (uint8_t i = 0; i < numDec; i++) uart_putc(UART_ID,'0');
        }
        if (laneFinishPosition[i]) {
            uart_putc(UART_ID, (char)(laneFinishPosition[i] - 1 + positionMarker[positionIndex]));
        }
        else {
            uart_putc(UART_ID, ' ');
        }
        uart_putc(UART_ID, ' ');
    }
    uart_puts(UART_ID, "\r\n");
}

bool are_we_done(void) {
    bool done = true;
    for (uint8_t i = 0; i < numLanes; i++) {
        if (laneEnabled[i] && !laneMasked[i]) {
            done = false;
        }
        else lane_enable(i, false);
    }
    if (done) {
        cancel_alarm(alarmID);
        racing = false;
        print_finish_order();
        gpio_set_irq_enabled(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    }
    return done;
}


int main() {
    uart_init(UART_ID, 2400);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    int actual = uart_set_baudrate(UART_ID, BAUD_RATE);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, DATA_BITS, STOP_BITS, PARITY);
    int UART_IRQ = UART_ID == uart0 ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(UART_IRQ, on_uart_rx);
    irq_set_enabled(UART_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
    uart_set_fifo_enabled(UART_ID, false);
    
    uart_puts(UART_ID, "\r\n\nPiderby 2021\r\n");

    gpio_set_irq_enabled_with_callback(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    for (uint8_t i = 0; i < numLanes; i++) {
        gpio_init(gpio[i]);
        gpio_pull_up(gpio[i]);
        laneMasked[i] = false;
    }

    while (1) {
        if (racing) {
            are_we_done();
        }
        sleep_us(10);
    }

    return 0;
}