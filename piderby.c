#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/i2c.h"
//#include "math.h"

#define UART_ID uart0
#define BAUD_RATE 9600
#define DATA_BITS 8
#define STOP_BITS 1
#define PARITY    UART_PARITY_NONE
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define GATE_PIN 22
#define I2C_PORT i2c0
#define SDAPIN 4
#define SCLPIN 5


uint8_t numLanes = 8;
uint8_t numDec = 4;
uint8_t gpio[] = {11,12,13,14,15,16,17,18};

//i2c addresses and commands
static int LaneAddresses[8] =  {0x75, 0x74, 0x73, 0x72, 0x71, 0x70, 0x69, 0x68};
uint8_t brightness_control=0x7A;
uint8_t clear_display=0x76;


uint64_t laneTicks[8];
uint8_t laneFinishPosition[8];
uint64_t previousTicks;
uint16_t positionAllowance = 1;
uint8_t char_rxed = 0;
uint8_t positionMarker[4]={'a','A','1','!'};
uint8_t positionIndex = 0;
uint8_t laneMarker[4] = {'A','1','a','A'};
char dnf[]="dnF";

uint8_t laneIndex = 0;
int32_t alarmID;
uint8_t resetTime = 0; // seconds between automatic reset between races

char msg[50];
char * msgptr=msg;

bool racing = false;
bool preparing = false;
bool masking = false;

struct lane {
    bool enabled;
    bool act_disable;
    bool masked;
};

struct lane lanes[8];

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
void send_times(void);
void send_positions(void);
void clear_displays(void);
void lane_text(uint8_t laneindex, const char* text);
void lane_time(uint8_t laneindex, const char* time);
void prepare_race(void);

void lane_enable(uint8_t laneindex, bool active) {
    gpio_set_irq_enabled(gpio[laneindex], GPIO_IRQ_EDGE_FALL, active);
    lanes[laneindex].enabled = active;
}

void i2c_write_byte(int laneindex, const uint8_t* val) {
    i2c_write_blocking(I2C_PORT, LaneAddresses[laneindex], val, 1, false);
    sleep_us(600);
}

void uart_print(const char* string) {
    uart_puts(UART_ID, string);
}
void uart_println(const char* string) {
    uart_puts(UART_ID, string);
    uart_puts(UART_ID, "\r\n");
}

void gate_switch_en(bool enable) {
    gpio_set_irq_enabled(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, enable);
}

int64_t alarm_callback(alarm_id_t id, void *user_data) {
    // do stuff to end the incomplete race
    uart_println("Race did not complete");
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, false);
    }
    return 0;
}

void reset(void) {
    gate_switch_en(true);
    racing = false;
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, false);
    }
    uart_println("OK");
}

void start_race() {
    alarmID = add_alarm_in_ms(10000, alarm_callback, NULL, false);
    for (uint8_t i = 0; i < numLanes; i++) {
        lane_enable(i, !lanes[i].masked);
        laneTicks[i] = previousTicks + 10000000;
    }
    clear_displays();
    uart_println("They're off!");
    gate_switch_en(false); 
    preparing = false;
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
                uart_println(convertido);
            }
            else {
                char substr[20];
                strcpy(substr, &(message[1]));
                positionAllowance = atoi(substr);
                uart_println("OK");
            }
            break;
        case 'R':
            switch (toupper(message[1])) {
                case 'A': // Force end of race, return results, then reset
                    racing = false;
                    for (uint8_t i = 0; i < numLanes; i++) {
                        lanes[i].act_disable = true;
                        //laneEnabled[i] = false;
                        //lane_enable(i, false);
                    }
                    print_finish_order();
                    break;
                case 'P': // Return reesults from previous race
                    print_finish_order();
                    break;
                case 'R': // Read reset switch - active if gate is closed and ready for race
                    uart_println((char[2]){(int)(!gpio_get(GATE_PIN)) + '0'});  // I know this looks crazy. we're jut creating a null terminated string from a single char, created by a boolean value
                    break;
                case 'S': // Read start switch - active is gate is open and racing
                    uart_println((char[2]){(int)(gpio_get(GATE_PIN)) + '0'});  // I know this looks crazy. we're jut creating a null terminated string from a single char, created by a boolean value
                    break;
                case 'L': // Read lane switches
                    for (uint8_t i = 0; i < numLanes; i++) {
                        if (!lanes[i].masked) uart_print((char[2]){(int)(gpio_get(gpio[i])) + '0'});
                        else uart_print("-");
                    }
                    uart_println("");
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
                        uart_println((char[2]){numDec + '0'});
                    }
                    else if ((message[2] - '0' > 2) && (message[2] - '0' < 6)) { // set the number of decimal places between 3 - 5
                        numDec = message[2] - '0';
                        uart_println("OK");
                    }
                    else uart_println("?");
                    break;
                case 'F': // set/read photo finish trigger delay -- GPRM sends 'of24'
                    uart_println("OK");
                    break;

                case 'L': // Set / Read lane character
                    if (message[2] == 0) {  // return the current marker
                        uart_println((char[2]){laneMarker[laneIndex]});
                    }
                    else if ((message[2] - '0' >= 0) && (message[2] - '0' < 4)) { // set the lane marker
                        laneIndex = message[2] - '0';
                        uart_println("OK");
                    }
                    else uart_println("?");
                    break;
                case 'M': // Set Read lane mask
                    if (message[2] == 0) { // show the masked lanes
                        for (uint8_t i = 0; i < numLanes; i++ ) {
                            if (lanes[i].masked) uart_print((char[2]){i + '1'});
                        }
                        uart_println("");
                    }
                    else { 
                        uint8_t chardec = message[2] - '0';
                        if (chardec > 0 && chardec < numLanes + 1) {
                            lanes[chardec - 1].masked = true;
                            uart_println("OK");
                        }
                        else if (chardec == 0) {
                            for (uint8_t i = 0; i < numLanes; i++ ) {
                                lanes[i].masked = false;
                            }
                            uart_println("OK");
                        }
                        else uart_println("?");
                    }
                    break;
                case 'N': // Set / Read number of lanes
                    if (message[2] == 0) {  // return the number of lanes
                        uart_println((char[2]){numLanes + '0'});
                    }
                    else if ((message[2] - '0' > 0) && (message[2] - '0' < 9)) { // set the number of lanes
                        numLanes = message[2] - '0';
                        uart_println("OK");
                    }
                    else uart_println("?");

                    break;
                case 'P': // Set / Read place character
                    if (message[2] == 0) {  // return the current marker
                        uart_println((char[2]){positionMarker[positionIndex]});
                    }
                    else if ((message[2] - '0' >= 0) && (message[2] - '0' < 4)) { // set the position marker
                        positionIndex = message[2] - '0';
                        uart_println("OK");
                    }
                    else uart_println("?");


                    break;
                case 'R': // Set / Read automatic reset delay (or/or0/orxx) - GPRM sends 'or0'
                    if (message[2] == 0) { // display the reset time
                        char convertido[6];
                        sprintf(convertido, "%u", resetTime);
                        uart_println(convertido);
                    }
                    else {
                        char substr[20];
                        strcpy(substr, &(message[2]));
                        resetTime = atoi(substr);
                        uart_println("OK");
                    }
                    break;
                case 'V': // Lane reversal (ov/ov1/ov0) - GPRM sends 'ov0'
                    uart_println("OK");
                    break;
                case 'W': // set / read photo finish trigger length - GPRM sends 'ow15'
                    uart_println("OK");
                    break;
                default:
                    uart_println("?");
                    break;
            }

            break;
        case 'V':
            uart_println("PiDerby");
            break;
        default: // no idea what to do
            uart_println("?");
            break;
    }
    message = '\0';
}

void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == GATE_PIN) {
        if (events == 0x8) {
            previousTicks = time_us_64 ();
            preparing = true;
        }
    }
    else {
        laneTicks[gpio - 11] = time_us_64 ();
        lanes[gpio - 11].act_disable = true;
    }
}

void process_finish_order (void) {
    for (uint8_t i = 0; i < numLanes; i++) {
        raceData[i].laneticks = (float)(laneTicks[i] - previousTicks) / 10;;
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
        uart_print ((char[2]){(laneMarker[laneIndex] + i)});
        uart_print("=");

        float time = (float)raceData[i].laneticks / 100000;
        char convertido[16];
        sprintf(convertido, "%.5f", time);

        if (!lanes[i].masked) {
            uint8_t strLength;
            if (time >= 10) strLength = numDec + 3;
            else strLength = numDec + 2;
            
            for (uint8_t i = 0; i < strLength; i++) uart_print((char[2]){convertido[i]});
        }
        else {
            uart_print("10.");
            for (uint8_t i = 0; i < numDec; i++) uart_print("0");
        }
        if (laneFinishPosition[i]) {
            uart_print((char[2]){laneFinishPosition[i] - 1 + positionMarker[positionIndex]});
        }
        else {
            uart_print(" ");
        }
        uart_print(" ");
    }
    uart_println("");
}

bool are_we_done(void) {
    bool done = true;
    for (uint8_t i = 0; i < numLanes; i++) {
        if (!lanes[i].masked) {
            if (lanes[i].act_disable) {
                lane_enable(i, false);
                lanes[i].act_disable = false;
            }
            if (lanes[i].enabled) done = false;
        }
    }
    if (done) {
        cancel_alarm(alarmID);
        racing = false;
        print_finish_order();
        gate_switch_en(true); //gpio_set_irq_enabled(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        send_times();
    }
    return done;
}

void lane_text(uint8_t laneindex, const char* text) {
    i2c_write_byte(laneindex, &clear_display);
    i2c_write_blocking(I2C_PORT, LaneAddresses[laneindex], text, strlen(text), false);
    sleep_us(600);
}

void lane_time(uint8_t laneindex, const char* time) {
    i2c_write_byte(laneindex, &clear_display);
    
    //write the time digits - skipping the decimal point (assuming a sub 10 second time)
    i2c_write_byte(laneindex, &(time[0]));
    i2c_write_byte(laneindex, &(time[2]));
    i2c_write_byte(laneindex, &(time[3]));
    i2c_write_byte(laneindex, &(time[4]));
    
    //write the decimal point
    char dec[3]={0x77,0x01,0}; // enable decimal control and turn on the first bit
    i2c_write_blocking(I2C_PORT, LaneAddresses[laneindex], dec, 2, false);
}


void send_times(void) {
    for (uint8_t i = 0; i < numLanes; i++) {
        float time = (float)raceData[i].laneticks / 100000;
        char convertido[16];
        sprintf(convertido, "%.3f", time);

        if (!lanes[i].masked) {
            if (time < 10) {
                lane_time(i,convertido);
            }
            else {
                lane_text(i,dnf);
            }
        }
        else {

        }
    }
}

void send_positions(void) {
    char message[5];
    message[0]=' ';
    message[1]=' ';
    message[3]=' ';
    message[4]='\0';
    for (uint8_t i = 0; i < numLanes; i++) {
        if (!lanes[i].masked && raceData[i].laneticks / 100000 < 10) {
            message[2]='0' + laneFinishPosition[i];
            lane_text(i,message);
        }
        else {
            lane_text(i,dnf);
        }
    }
}

void clear_displays(void) {
    for (uint8_t i = 0; i < numLanes; i++) {
        sleep_us(600);
        i2c_write_byte(i, &clear_display);
    }
}

int main() {
    //setup i2c
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(SDAPIN, GPIO_FUNC_I2C);
    gpio_set_function(SCLPIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDAPIN); // i2c bus requires pull-ups on the data and clock lines
    gpio_pull_up(SCLPIN);
    
    //setup uart
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
    

    uart_println("PiDerby");
    
    uint8_t lane[]=" L  ";
    
    uint8_t brightness=10;
    
    
    for (uint8_t i = 0; i < numLanes; i++) {
        i2c_write_byte(i, &brightness_control);
        i2c_write_blocking(I2C_PORT, LaneAddresses[i] , &brightness, 2, false);
        sleep_us(600);
        i2c_write_byte(i, &clear_display);
        lane[2]='1' + i;
        lane_text(i, lane);
        //i2c_write_blocking(I2C_PORT, LaneAddresses[i] , lane, strlen(lane), false);
    }
    
    gpio_set_irq_enabled_with_callback(GATE_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_callback);

    for (uint8_t i = 0; i < numLanes; i++) {
        gpio_init(gpio[i]);
        gpio_pull_up(gpio[i]);
        lanes[i].masked = false;
    }

    while (1) {
        if (racing) {
            are_we_done();
        }
        else if (preparing) {
            start_race();
        }
    }

    return 0;
}