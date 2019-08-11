/* mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"

#define SLEEP_TIME                  10 // (msec)
#define APA102_COUNT                60
#define APA102_LEVEL                2 // brightness 0-31
#define SPI_RATE_HZ                 500000                   

/* Pin assignments:
 * Clock (APA102s): SPI3 SCLK, PB_3 (D13) 
 * Data (APA102s): SPI3 MOSI, PB_5 (D11)
 * If APA102 class requires MISO, PB_4 may be sacrificed.
 * TAILLIGHT_EN: PWM1/1, PA_9 (D8)
 * FLOOD_L_EN: PWM1/2, PA_9 (D1)
 * FLOOD_R_EN: PWM1/3, PA_10 (D0)
 * CHG_EN: PB_1 (D6)
 * SPEED_IN: PA_0 (A0)
 * CTRL_1: PA_1 (A1)
 * CTRL_2: PA_2 (A2)
 * CTRL_3: PA_3 (A3)
 * CTRL_4: PA_4 (A4)
 * CTRL_5: PA_5 (A5)
 * CTRL_6: PA_6 (A6)
 * CTRL_7: PA_7 (A7)
 */

SPI spi(PB_5, PB_4, PB_3);

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

void apa102_draw(void) {
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

void color_advance(void) {
    int temp = color_buffer[APA102_COUNT - 1];
    for (int x = (APA102_COUNT - 1); x > 0; x--) {
        color_buffer[x] = color_buffer[x - 1];
        color_buffer[0] = temp;
    }
}

// main() runs in its own thread in the OS
int main()
{

    int i, c;
    int color = 1;

    spi.frequency(SPI_RATE_HZ);

    while (true) {
        // write out the current buffer
        apa102_draw();
        // advance the buffer one pixel
        color_advance();
        wait_ms(SLEEP_TIME);
    }
}
