/* LEDclock Firmware
 * Developed for Atmel ATTiny2313 MCU
 * HW rev A
 *
 * LEDclock project is a simple, yet very big and bright clock
 * based on chinesium 3V LED filaments (designed to look like a
 * incadescent bulb tungsten filament... from the distance, I guess)
 *
 * Features of this FW:
 * - 24 hour clock,
 * - time setting via two buttons (one for minutes, one for hours),
 * - long press button to set time faster,
 * - blinking after power loss to indicate that the time is incorrect,
 * - RTC calibration with 1 ppm precision (+- 1023 ppms, hardcoded),
 * - slow, gradual enabling/disabling changed screen segments (PWM),
 * - brightness setting (hardcoded),
 * - watchdog.
 *
 * Copyright 2022 Aleksander Kaminski
 *
 * Free for non-commercial use and education purposes.
 */

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define BUTTON_COOLDOWN  200  /* In about 1 ms */
#define BUTTON_LONGPRESS 2000 /* In about 1 ms */
#define LONGPRESS_HZ     4    /* How fast is autopress working */
#define BRIGHTNESS       250  /* Duty of LCD being on (x/256) */
#define RTC_CALIB        0    /* +-ppm */
#define RTC_HZ           1024
#define RAMP_MIN         16   /* Minimal PWM (x/256) */
#define RAMP_MAX         (BRIGHTNESS - 16)
#define RAMP_INC         4    /* Increased on every screen refresh (122 Hz) */


typedef unsigned char byte;


int g_subseconds;
unsigned int g_seconds_calib_cnt;
byte g_seconds;
byte g_minutes_h;
byte g_minutes_l;
byte g_hours_h = 1;
byte g_hours_l = 2;
byte g_time_set;

byte g_led_on[4];
byte g_led_rampup[4];
byte g_led_rampdown[4];
byte g_rampcnt = RAMP_MIN;
byte g_curr_digit;

unsigned int g_button_presscnt[2];
enum {
	button_not_active,
	button_active,
	button_longpress } g_button_state[2] = {
		button_not_active, button_not_active
	};


static void _hours_inc(void)
{
	byte hours_term = 10;

	if (g_hours_h == 2)
		hours_term = 4;

	if (++g_hours_l < hours_term)
		return;
	g_hours_l = 0;

	if (++g_hours_h < 3)
		return;
	g_hours_h = 0;
}


static void _minutes_inc(void)
{
	if (++g_minutes_l < 10)
		return;
	g_minutes_l = 0;

	if (++g_minutes_h < 6)
		return;
	g_minutes_h = 0;

	_hours_inc();
}


static byte button_check(byte which)
{
	return !(PIND & (1 << which));
}


static byte _button_handle(byte which)
{
	byte trigger = 0;

	if (button_check(which)) {
		if (g_button_presscnt[which] < BUTTON_LONGPRESS) {
			++g_button_presscnt[which];

			if (g_button_state[which] == button_not_active &&
					g_button_presscnt[which] >= BUTTON_COOLDOWN) {
				g_button_state[which] = button_active;
				trigger = 1;
			}
		}
		else {
			g_button_state[which] = button_longpress;
		}
	}
	else {
		g_button_state[which] = button_not_active;
		g_button_presscnt[which] = 0;
	}

	return trigger;
}


static byte decode7seg(byte dig)
{
	static const byte lut[] = {
		~0x3f & 0x7f, /* 0 */
		~0x06 & 0x7f, /* 1 */
		~0x5b & 0x7f, /* 2 */
		~0x4f & 0x7f, /* 3 */
		~0x66 & 0x7f, /* 4 */
		~0x6d & 0x7f, /* 5 */
		~0x7d & 0x7f, /* 6 */
		~0x07 & 0x7f, /* 7 */
		~0x7f & 0x7f, /* 8 */
		~0x6f & 0x7f, /* 9 */
		~0x00 & 0x7f  /* void */
	};

	return lut[dig];
}


static void _update_digit(byte which, byte newval)
{
	g_led_on[which] |= g_led_rampup[which];
	g_led_on[which] &= ~g_led_rampdown[which];

	byte diff = g_led_on[which] ^ newval;

	g_led_rampup[which] = diff & ~g_led_on[which];
	g_led_rampdown[which] = diff & g_led_on[which];
}


static void _set_ramp(byte val)
{
	if (val < RAMP_MIN)
		val = RAMP_MIN;
	else if (val > RAMP_MAX)
		val = RAMP_MAX;

	OCR0A = val;
	g_rampcnt = val;
}


static void set_brightness(byte val)
{
	OCR0B = val;
}


static void _refresh_screen(int blanking)
{
	if (blanking) {
		for (byte i = 0; i < 4; ++i)
			_update_digit(i, 10);
	}
	else {
		_update_digit(0, decode7seg(g_hours_h));
		_update_digit(1, decode7seg(g_hours_l));
		_update_digit(2, decode7seg(g_minutes_h));
		_update_digit(3, decode7seg(g_minutes_l));
	}

	_set_ramp(RAMP_MIN);
}


static void set_dots(int state)
{
	PORTB &= ~(!state << 7);
	PORTB |= !state << 7;
}


ISR(INT0_vect)
{
	byte update = 0, blanking = 0, btrigger = 0;

	wdt_reset();

	if (++g_subseconds >= RTC_HZ) {
		g_subseconds -= RTC_HZ;
		if (++g_seconds >= 60) {
			_minutes_inc();
			update = 1;
		}

		if (g_seconds & 1) {
			set_dots(1);
			blanking = 1;
		}
		else {
			set_dots(0);
		}

		if (!g_time_set)
			update = 1;
		else
			blanking = 0;

		if (++g_seconds_calib_cnt >= RTC_HZ) {
			g_subseconds += RTC_CALIB;
			g_seconds_calib_cnt = 0;
		}
	}

	if ((btrigger = _button_handle(0))) {
		_minutes_inc();
	}
	else if ((btrigger = _button_handle(1))) {
		_hours_inc();
	}
	else if (!(g_subseconds % (RTC_HZ / LONGPRESS_HZ))) {
		btrigger = 1;
		if (g_button_state[0] == button_longpress)
			_minutes_inc();
		else if (g_button_state[1] == button_longpress)
			_hours_inc();
		else
			btrigger = 0;
	}

	if (btrigger) {
		update = 1;
		g_time_set = 1;
	}

	if (update) {
		g_seconds = 0;
		_refresh_screen(blanking);
	}
}


/* Select new digit */
ISR(TIMER0_OVF_vect)
{
	PORTB = (g_led_on[g_curr_digit] |
		g_led_rampup[g_curr_digit]) &
		~g_led_rampdown[g_curr_digit];
	PORTD &= ~(1 << (3 + g_curr_digit));
}


/* Stop ramp-up, start ramp-down */
ISR(TIMER0_COMPA_vect)
{
	if (g_rampcnt < RAMP_MAX) {
		byte t = PORTB & ~(g_led_rampup[g_curr_digit]);
		PORTB = t | g_led_rampdown[g_curr_digit];
	}
}


/* Disable screen (brightness control) */
ISR(TIMER0_COMPB_vect)
{
	PORTB = 0;
	PORTD |= 0xf << 3;
	g_curr_digit = (g_curr_digit + 1) % 4;

	if (!g_curr_digit)
		_set_ramp(g_rampcnt + RAMP_INC);
}


int main(void)
{
	wdt_enable(WDTO_250MS);
	wdt_reset();

	/* Init screen */
	PORTB = 0;
	DDRB = 0xff;
	PORTD |= 0xf << 3;
	DDRD |= 0xf << 3;
	_refresh_screen(0);

	/* Buttons - inputs, pull-up enable */
	PORTD |= (1 << 1) | (1 << 0);

	/* Real time clock interrupt generated by an external IC
	 * every 1/1024th of a second */
	DDRD &= ~(1 << 2);
	PORTD &= ~(1 << 2);
	/* Generate interrupt INT0 on rising edge */
	MCUCR |= (1 << ISC01) | (1 << ISC00);
	GIMSK |= 1 << INT0;

	/* Timer0 - screen management */
	/* Update OCRx at MAX */
	TCCR0A = (1 << WGM01) | (1 << WGM00);
	_set_ramp(RAMP_MIN);
	set_brightness(BRIGHTNESS);
	TIMSK |= (1 << OCIE0B) | (1 << TOIE0) | (1 << OCIE0A);
	/* Enable counter (1/64 prescaler) */
	TCCR0B = (1 << CS01) | (1 << CS00);

	sleep_enable();
	sei();

	while (1)
		sleep_cpu();

	return 0;
}

