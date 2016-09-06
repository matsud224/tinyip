#include "led.h"
#include "mbed.h"

DigitalOut led_blue(LED_BLUE);
DigitalOut led_red(LED_RED);
DigitalOut led_green(LED_GREEN);
DigitalOut led_user(LED_USER);

void mcled_change(int color){
	led_blue = color & 0x1;
	led_green = color & 0x2;
	led_red = color & 0x4;
}

void redled_off(){
	led_user = 1;
}

void redled_on(){
	led_user = 0;
}
