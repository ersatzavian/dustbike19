/* mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"

//#define DEBUG

#define UPDATE_PERIOD_MS            100 // (msec)
#define APA102_COUNT                60
#define APA102_LEVEL                31 // brightness 0-31
#define SPI_RATE_HZ                 500000  
#define PWM_RATE_HZ                 500
#define WHEEL_CIRC_MM               2170.0 // wheel circumference in millimeters.
#define NUM_WHEEL_MAGNETS           2 // number of evenly-spaced speed sensor magnets
#define PIXELS_PER_METER            60.0
#define FULL_SPEED_MPS              6.7 // about 15 mph; set the taillight on full at this speed.
#define MAX_SPEED_MPS               18.0 // about 40 mph; used to debounce the mag switch
#define TEST_TIME_SINCE_WHEELTICK_US     2000000 // 100 ms/tick * 4 ticks/rev * 1 rev/2.17m = 0.184 s/m = 5.4 m/s = 12mph
#define WIG_WAG_RATE_S              1.0
#define PATROL_COLOR_RATE_S         0.4
#define MIN_TIME_BETWEEN_TICKS_US  (WHEEL_CIRC_MM * 1000)/ (NUM_WHEEL_MAGNETS * MAX_SPEED_MPS)

/* Pin assignments */ 
#define CLK_PIN                 PB_3  // D13
#define DATA_PIN                PB_5  // D11
#define MISO_UNUSED_PIN         PB_4  // D12
#define TAILLIGHT_EN_PIN        PA_8  // D9
#define FLOOD_L_EN_PIN          PA_9  // D1
#define FLOOD_R_EN_PIN          PA_10 // D0
#define CHG_EN_PIN              PB_1  // D6
#define SPEED_IN_PIN            PA_0  // A0
#define METER_EN_PIN            PB_0  // D3
#define CTRL_1_PIN              PA_1  // A1
#define CTRL_2_PIN              PA_3  // A2
#define CTRL_3_PIN              PA_4  // A3
#define CTRL_4_PIN              PA_5  // A4
#define CTRL_5_PIN              PA_6  // A5
#define CTRL_6_PIN              PA_7  // A6
#define CTRL_7_PIN              PA_2  // A7
// virtual com port collides with control 7 (used for panel meter enable)
// PA2 = TX from MCU
// PA15 = RX to MCU
#ifdef DEBUG
Serial pc(USBTX, USBRX);
#endif 

SPI spi(DATA_PIN, MISO_UNUSED_PIN, CLK_PIN);
DigitalOut taillight(TAILLIGHT_EN_PIN);
// NFET Switches 2-4 have high threshold voltages and have external pullups
// Use DigitalInOut instead of DigitalOut so we can configure Open Drain
DigitalOut floods(FLOOD_R_EN_PIN);
DigitalInOut meter_en(METER_EN_PIN);
DigitalOut chg_en(CHG_EN_PIN, 0);

// Inputs
DigitalIn ctrl1(CTRL_1_PIN, PullUp);
DigitalIn ctrl2(CTRL_2_PIN, PullUp);
DigitalIn ctrl3(CTRL_3_PIN, PullUp);
DigitalIn ctrl4(CTRL_4_PIN, PullUp);
DigitalIn ctrl5(CTRL_5_PIN, PullUp);
DigitalIn ctrl6(CTRL_6_PIN, PullUp);
#ifndef DEBUG
DigitalIn ctrl7(CTRL_7_PIN, PullUp);
#endif

InterruptIn spd(SPEED_IN_PIN, PullUp);

float time_since_wheeltick = TEST_TIME_SINCE_WHEELTICK_US;

Ticker tick_250us;
Ticker tick_100ms;
Timer t;

int count_250us_colors = 0;
int count_100ms_patrol = 0;
int count_100ms_wigwag = 0;

float speed_mps = 0;

bool colors_active = false;
bool floods_active = false;
bool wig_wag_active = false;
bool patrol_mode_active = false;

// Not HTML hex; APA102 uses [B,G,R]
int color_buffer[] = {0x2a034d,0x27054f,0x250851,0x220b53,0x200e55,0x1d1157,
                      0x1b1459,0x18175b,0x16195d,0x131c5f,0x111f61,0x0e2263,
                      0x0c2565,0x092867,0x072b69,0x082b69,0x092867,0x0c2565,
                      0x0e2263,0x111f61,0x131c5f,0x16195d,0x18175b,0x1b1459,
                      0x1d1157,0x200e55,0x220b53,0x250851,0x27054f,0x2a034d,
                      0x2a034d,0x27054f,0x250851,0x220b53,0x200e55,0x1d1157,
                      0x1b1459,0x18175b,0x16195d,0x131c5f,0x111f61,0x0e2263,
                      0x0c2565,0x092867,0x072b69,0x082b69,0x092867,0x0c2565,
                      0x0e2263,0x111f61,0x131c5f,0x16195d,0x18175b,0x1b1459,
                      0x1d1157,0x200e55,0x220b53,0x250851,0x27054f,0x2a034d                    
                    };

int patrol_color_buffer[] = {0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,
                            0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,
                            0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,
                            0xff0000,0xff0000,0x0000ff,0x0000ff,0x0000ff,0x0000ff,
                            0x0000ff,0x0000ff,0x0000ff,0x0000ff,0x0000ff,0x0000ff,
                            0x0000ff,0x0000ff,0x0000ff,0x0000ff,0x0000ff,0x0000ff,
                            0x0000ff,0x0000ff,0x0000ff,0x0000ff,0xff0000,0xff0000,
                            0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,
                            0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,
                            0xff0000,0xff0000,0xff0000,0xff0000,0xff0000,0xff0000
                            };

void color_draw() {
    int i; 

    // start of frame
    for (i = 0; i < 4; i++) {
        spi.write(0);
    }
    // LED buffer
    for (i = 0; i < APA102_COUNT; i++) {
        spi.write((7<<5) | APA102_LEVEL);
        spi.write(color_buffer[i] & 0xff); // B
        spi.write((color_buffer[i] >> 8) & 0xff); // G
        spi.write((color_buffer[i] >> 16) & 0xff); // R
    }
    // end of frame
    for (i = 0; i < 4; i++) {
        spi.write(1);
    }
}

void color_advance() {
    int temp = color_buffer[0];
    for (int x = 0; x < (APA102_COUNT - 1); x++) {
        color_buffer[x] = color_buffer[x + 1];
        color_buffer[APA102_COUNT - 1] = temp;
    }

    // write out the current buffer
    color_draw();
}

void patrol_draw() {
    int i; 

    // start of frame
    for (i = 0; i < 4; i++) {
        spi.write(0);
    }
    // LED buffer
    for (i = 0; i < APA102_COUNT; i++) {
        spi.write((7<<5) | APA102_LEVEL);
        spi.write(patrol_color_buffer[i] & 0xff); // B
        spi.write((patrol_color_buffer[i] >> 8) & 0xff); // G
        spi.write((patrol_color_buffer[i] >> 16) & 0xff); // R
    }
    // end of frame
    for (i = 0; i < 4; i++) {
        spi.write(1);
    }
}

void patrol_color_advance() {
    for (int x = 0; x < (APA102_COUNT - 1); x++) {
        if (patrol_color_buffer[x] == 0xff0000) {
            // swap red to blue
            patrol_color_buffer[x] = 0x0000ff;
        } else {
            // swap anything but red to red
            patrol_color_buffer[x] = 0xff0000;
        }
    }

    // write out the current buffer
    patrol_draw();
}

void color_clear() {
    int i; 

    // start of frame
    for (i = 0; i < 4; i++) {
        spi.write(0);
    }
    // LED buffer
    for (i = 0; i < APA102_COUNT; i++) {
        spi.write((7<<5) | APA102_LEVEL);
        spi.write(0);
        spi.write(0);
        spi.write(0);
    }
    // end of frame
    for (i = 0; i < 4; i++) {
        spi.write(1);
    }
}

void service_ctrl_panel() {
    bool start_wig_wag = false;

    // service control panel
    // 1: Charge Enable
    if (!ctrl1.read()) {
        chg_en.write(1);
    } else {
        chg_en.write(0);
    }
    // 2: colors_active
    if (!ctrl2.read()) {
        colors_active = true;
    } else {
        colors_active = false;
    }

    // 3: Wig Wag (disabled until power board can be corrected)
    // if (!ctrl3.read()) {
    //     wig_wag_active = true;
    // } else {
    //     wig_wag_active = false;
    // }

    // 4: Floods
    if (!ctrl4.read()) {
        floods_active = true; 
    } else {
        floods_active = false;
    } 

    // 5: Taillight
    if (!ctrl5.read()) {  
        taillight.write(1);
    } else {
        taillight.write(0);
    }
    // 6: Patrol mode
    if (!ctrl6.read()) {
        patrol_mode_active = true;
    } else {
        patrol_mode_active = false;
    }
    // 7: Panel Meter Enable
    #ifndef DEBUG
    if (!ctrl7.read()) {
        meter_en.write(1);
    } else {
        meter_en.write(0);
    }
    #endif

    // Patrol mode overrides normal color mode
    if (patrol_mode_active) {
        colors_active = false;
    }

    // Wig-wag overrides floods
    // if (wig_wag_active) {
    //     if (start_wig_wag) {
    //         flood_l.write(1);
    //         flood_r.write(0);
    //     }
    // } else {
        if (floods_active) {
            floods.write(1);
            // flood_l.write(1);
            // flood_r.write(1);
        } else {
            floods.write(0);
            // flood_l.write(0);
            // flood_r.write(0);
        }
    // }
}

void callback_spd_pin() {
    // disable IRQ till we're out of here
    spd.disable_irq();

    time_since_wheeltick = t.read_us();
    // debounce the magnetic switch by discarding reads that are too close together to be real
    if (time_since_wheeltick < MIN_TIME_BETWEEN_TICKS_US) {
        spd.enable_irq();
        return;
    }

    // speed in meters per second works out nicely since mm / ms = m/s
    speed_mps = (WHEEL_CIRC_MM / NUM_WHEEL_MAGNETS) / (time_since_wheeltick / 1000.0);
    t.reset();

    spd.enable_irq();
}

void callback_250us() {
    count_250us_colors++;
}

void callback_100ms() {
    count_100ms_patrol++;
    count_100ms_wigwag++;
}

int main()
{
    // configure DigitalInOut pins as open drain ASAP
    meter_en.output();
    meter_en.mode(OpenDrain);

    // rest of digital outputs
    floods.write(0);
    meter_en.write(0);
    taillight.write(0);

    // initialize SPI for APA102 interface
    spi.frequency(SPI_RATE_HZ);

    // initialize the lights to on or off
    // patrol mode overrides normal color mode
    // bools are mutually exclusive coming from the control panel reader
    // but let's keep it tidy here anyway
    if (patrol_mode_active) {
        patrol_draw();
    } else if (colors_active) {
        color_draw();
    } else {
        color_clear();
    }

    // start the speed counter timer
    t.start();
    //attach the speed counter callback
    spd.fall(&callback_spd_pin);

    tick_250us.attach_us(&callback_250us, 250);
    tick_100ms.attach(&callback_100ms, 0.1);

    #ifdef DEBUG
    pc.printf("Ready, min time between ticks is %0.2f\r\n", MIN_TIME_BETWEEN_TICKS_US);
    #endif 

    while (true) {
        // read the controls and update taillight brightness
        service_ctrl_panel();

        // the 250us counter drives the advance rate of the pixels
        // here we are counting on colors_active and patrol_mode_active to be mutually exclusive...
        if (count_250us_colors >= (4000.0 * (1.0/PIXELS_PER_METER) / speed_mps)) {
            if (colors_active) {
                color_advance();
            } else if (!patrol_mode_active) {
                // clean up after other modes since this one is the fastest;
                // if nobody else is using the lights, clear them here
                color_clear();
            }
            count_250us_colors = 0;
        }

        // this 100ms counter drives the color change rate in patrol mode 
        if (count_100ms_patrol / 10.0 >= PATROL_COLOR_RATE_S) {
            if (patrol_mode_active) {
                patrol_color_advance();
            } 
            count_100ms_patrol = 0;
        }
        
        // this 100ms counter drives the blink rate of the wigwag
        if (count_100ms_wigwag / 10.0 >= WIG_WAG_RATE_S) {
            // if (wig_wag_active) {
            //     flood_l.write(1 - flood_l.read());
            //     flood_r.write(1 - flood_r.read());
            // } 
            #ifdef DEBUG
            pc.printf("t = %0.2f, speed = %0.2f m/s\r\n", time_since_wheeltick, speed_mps);
            #endif
            count_100ms_wigwag = 0;
        }
    }
}
