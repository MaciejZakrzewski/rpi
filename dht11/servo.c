// http://www.airspayce.com/mikem/bcm2835/bcm2835-1.50.tar.gz

// tar zxvf bcm2835-1.xx.tar.gz
// cd bcm2835-1.xx
// ./configure
// make
// sudo make check
// sudo make install

// gcc -o servo servo.c -l bcm2835 
// sudo ./servo

#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define PIN RPI_GPIO_P1_12
#define PWM_CHANNEL 0
#define RANGE 4000
#define DIVIDER 96

// from SG90 servo specification in ms
#define SERVO_DUTY_CYCLE 20
#define MIN_PULSE_WIDTH 0.5
#define MAX_PULSE_WIDTH 2.5

#define MIN_TEMP 0
#define MAX_TEMP 50

void writeMiliseconds(float value){
    if (value < MIN_PULSE_WIDTH) {
        value = MIN_PULSE_WIDTH;
    }
    if (value > MAX_PULSE_WIDTH){
        value = MAX_PULSE_WIDTH;
    }
    bcm2835_pwm_set_data(PWM_CHANNEL, value / SERVO_DUTY_CYCLE * RANGE);
}

void writeTemp(float value){
    if(value < MIN_TEMP){
        value = MIN_TEMP;
    }
    if(value > MAX_TEMP){
        value = MAX_TEMP;
    }
    writeMiliseconds(MIN_PULSE_WIDTH + value / MAX_TEMP * 2);
}

void writeAngle(float value){
    writeMiliseconds(MIN_PULSE_WIDTH + value / 180 * 2);
}

void testServo(){
    for(int i=0; i < 6; ++i) {
	writeAngle(i*30);
        delay(500);
    }
}

int main(int argc, char **argv)
{
    if (!bcm2835_init()){
        return 1;
    }

    bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_ALT5);

    // Divider 96
    // Range 4000
    // 19.2 / 96 / 4000 = 50Hz
    bcm2835_pwm_set_clock(DIVIDER);
    bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
    bcm2835_pwm_set_range(PWM_CHANNEL, RANGE);

   // testServo();
   // writeMiliseconds(1);

    writeTemp(atof(argv[1]));

    delay(1000);
    bcm2835_close();
    return 0;
}
