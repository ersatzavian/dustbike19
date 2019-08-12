/* mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"

#define UPDATE_PERIOD_MS            100 // (msec)
#define APA102_COUNT                60
#define APA102_LEVEL                4 // brightness 0-31
#define SPI_RATE_HZ                 500000  
#define PWM_RATE_HZ                 500
#define WHEEL_CIRC_MM               2170 // wheel circumference in millimeters.
#define NUM_WHEEL_MAGNETS           4.0 // number of evenly-spaced speed sensor magnets
#define PIXELS_PER_METER            60.0
#define FULL_SPEED_MPS              6.7 // about 15 mph; set the taillight on full at this speed.
#define TEST_TIME_SINCE_WHEELTICK_MS     2000 // 100 ms/tick * 4 ticks/rev * 1 rev/2.17m = 0.184 s/m = 5.4 m/s = 12mph
#define WIG_WAG_RATE_S              1.0

/* Pin assignments */ 
#define CLK_PIN                 PB_3
#define DATA_PIN                PB_5
#define MISO_UNUSED_PIN         PB_4
#define TAILLIGHT_EN_PIN        PA_8
#define FLOOD_L_EN_PIN          PA_9
#define FLOOD_R_EN_PIN          PA_10
#define CHG_EN_PIN              PB_1
#define SPEED_IN_PIN            PA_0
#define METER_EN_PIN            PB_0
#define CTRL_1_PIN              PA_1
#define CTRL_2_PIN              PA_3
#define CTRL_3_PIN              PA_4
#define CTRL_4_PIN              PA_5
#define CTRL_5_PIN              PA_6
#define CTRL_6_PIN              PA_7
#define CTRL_7_PIN              PA_2

SPI spi(DATA_PIN, MISO_UNUSED_PIN, CLK_PIN);
PwmOut taillight(TAILLIGHT_EN_PIN);
// NFET Switches 2-4 have high threshold voltages and have external pullups
// Use DigitalInOut instead of DigitalOut so we can configure Open Drain
DigitalInOut flood_l(FLOOD_L_EN_PIN);
DigitalInOut flood_r(FLOOD_R_EN_PIN);
DigitalInOut meter_en(METER_EN_PIN);
DigitalOut chg_en(CHG_EN_PIN, 0);

// Inputs
DigitalIn ctrl1(CTRL_1_PIN, PullUp);
DigitalIn ctrl2(CTRL_2_PIN, PullUp);
DigitalIn ctrl3(CTRL_3_PIN, PullUp);
DigitalIn ctrl4(CTRL_4_PIN, PullUp);
DigitalIn ctrl5(CTRL_5_PIN, PullUp);
DigitalIn ctrl6(CTRL_6_PIN, PullUp);
DigitalIn ctrl7(CTRL_7_PIN, PullUp);


int time_since_wheeltick_ms = TEST_TIME_SINCE_WHEELTICK_MS;

Ticker tick_250us;
Ticker tick_100ms;

int count_250us = 0;
int count_100ms = 0;

float speed_mps = 0;

bool colors_active = false;
bool taillight_active = false;
bool floods_active = false;
bool wig_wag_active = false;

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

void apa102_draw() {
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

void color_advance() {
    int temp = color_buffer[APA102_COUNT - 1];
    for (int x = (APA102_COUNT - 1); x > 0; x--) {
        color_buffer[x] = color_buffer[x - 1];
        color_buffer[0] = temp;
    }

    // write out the current buffer
    apa102_draw();
}

void callback_250us() {
    // speed in meters per second works out nicely since mm / ms = m/s
    speed_mps = (WHEEL_CIRC_MM / NUM_WHEEL_MAGNETS) / time_since_wheeltick_ms;
    
    count_250us++;
}

void callback_100ms() {
    bool start_wig_wag = false;

    // taillight brightness proportional to speed, saturate at ~15 mph.
    if (taillight_active) {
        taillight.write(speed_mps / FULL_SPEED_MPS);
    } else {
        taillight.write(0);
    }

    // service control panel
    // 1: Color Lights
    if (!ctrl1.read()) {
        colors_active = true;
    } else {
        colors_active = false;
    }
    // 2: Taillight
    if (!ctrl2.read()) {
        taillight_active = true;
    } else {
        taillight_active = false;
    }
    // 3: Floods
    if (!ctrl3.read()) {
        floods_active = true; 
    } else {
        floods_active = false;
    } 
    // 4: Wig Wag
    if (!ctrl4.read()) {
        if (!wig_wag_active) {
            // wig wag just started
            start_wig_wag = true;
        }
        wig_wag_active = true;
    } else {
        wig_wag_active = false;
    }
    // 5: Charge Enable
    if (!ctrl5) {
        chg_en.write(1);
    } else {
        chg_en.write(0);
    }
    // 6: Panel Meter Enable
    if (!ctrl6) {
        meter_en.write(1);
    } else {
        meter_en.write(0);
    }

    // Wig-Wag overrides floods 
    if (wig_wag_active) {
        if (start_wig_wag) {
            flood_l.write(1);
            flood_r.write(0);
        }
    } else {
        if (floods_active) {
            flood_l.write(1);
            flood_r.write(1);
        } else {
            flood_l.write(0);
            flood_r.write(0);
        }
    }

    count_100ms++;
}

// main() runs in its own thread in the OS
int main()
{
    // configure DigitalInOut pins as open drain ASAP
    flood_l.output();
    flood_l.mode(OpenDrain);
    flood_r.output();
    flood_r.mode(OpenDrain);
    meter_en.output();
    meter_en.mode(OpenDrain);
    flood_l.write(0);
    flood_r.write(0);
    meter_en.write(0);

    spi.frequency(SPI_RATE_HZ);

    taillight.period(1.0/PWM_RATE_HZ);
    taillight.write(0);

    // draw out the starting frame on the color tape 
    apa102_draw();

    tick_250us.attach_us(&callback_250us, 250);
    tick_100ms.attach(&callback_100ms, 0.1);

    while (true) {
        // the 250us counter drives the advance rate of the pixels
        if (count_250us >= (4000.0 * (1.0/PIXELS_PER_METER) / speed_mps)) {
            if (colors_active) {
                color_advance();
            } else {
                color_clear();
            }
            count_250us = 0;
        }
        
        // the 100ms counter drives the blink rate of the wigwag
        // the taillight brightness and control states are also served in the 100ms callback.
        if (count_100ms / 10.0 >= WIG_WAG_RATE_S) {
            if (wig_wag_active) {
                flood_l.write(1 - flood_l.read());
                flood_r.write(1 - flood_r.read());
            } 
            count_100ms = 0;
        }
    }
}
