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
 * - RTC calibration with 1 ppm precision (+- 999 ppms),
 * - slow, gradual enabling/disabling changed screen segments (PWM),
 * - brightness setting (0-7),
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
#define BRIGHTNESS       140  /* Base brightness (x/256) */
#define RTC_CALIB        0    /* +-ppm */
#define RTC_HZ           1024
#define RAMP_MIN         16   /* Minimal PWM (x/256) */
#define RAMP_MAX         (BRIGHTNESS + (g_brightness * 16) - 16)
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
	button_longpress,
	button_lockup
} g_button_state[2] = { button_not_active, button_not_active };

enum {
	mode_normal = 0,
	mode_calib,
	mode_brightness,
	mode_end
} g_mode = mode_normal;
byte g_mode_timeout;
int g_rtc_calib = RTC_CALIB;
byte g_brightness = 4;


static void set_brightness(void)
{
	OCR0B = BRIGHTNESS + (g_brightness * 16);
}


static void brightness_inc(void)
{
	if (g_brightness < 8)
		++g_brightness;

	set_brightness();
}


static void brightness_dec(void)
{
	if (g_brightness > 0)
		--g_brightness;

	set_brightness();
}


static void calib_inc(void)
{
	if (g_rtc_calib < 1000)
		++g_rtc_calib;
}


static void calib_dec(void)
{
	if (g_rtc_calib > -1000)
		--g_rtc_calib;
}


static void hours_inc(void)
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


static void minutes_inc(void)
{
	if (++g_minutes_l < 10)
		return;
	g_minutes_l = 0;

	if (++g_minutes_h < 6)
		return;
	g_minutes_h = 0;

	hours_inc();
}


static byte button_check(byte which)
{
	return !(PIND & (1 << which));
}


static byte button_handle(byte which)
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
		else if (g_button_state[which] != button_lockup) {
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
		~0x00 & 0x7f, /* void */
		~0x7c & 0x7f, /* b */
		~0x39 & 0x7f, /* c */
		~0x79 & 0x7f  /* e */
	};

	return lut[dig];
}


static void update_digit(byte which, byte newval)
{
	g_led_on[which] |= g_led_rampup[which];
	g_led_on[which] &= ~g_led_rampdown[which];

	byte diff = g_led_on[which] ^ newval;

	g_led_rampup[which] = diff & ~g_led_on[which];
	g_led_rampdown[which] = diff & g_led_on[which];
}


static void set_ramp(byte val)
{
	if (val < RAMP_MIN)
		val = RAMP_MIN;
	else if (val > RAMP_MAX)
		val = RAMP_MAX;

	OCR0A = val;
	g_rampcnt = val;
}


static void refresh_screen(int blanking)
{
	switch (g_mode) {
		case mode_calib:
			if (g_rtc_calib >= 0)
				update_digit(0, decode7seg(0xc));
			else
				update_digit(0, decode7seg(0xe));

			update_digit(1, decode7seg((g_rtc_calib / 100) % 10));
			update_digit(2, decode7seg((g_rtc_calib / 10) % 10));
			update_digit(3, decode7seg(g_rtc_calib % 10));
			break;

		case mode_brightness:
			update_digit(0, decode7seg(0xb));
			update_digit(1, decode7seg(10)); /* void */
			update_digit(2, decode7seg(10)); /* void */
			update_digit(3, decode7seg(g_brightness));
			break;

		default:
			if (blanking) {
				for (byte i = 0; i < 4; ++i)
					update_digit(i, 10); /* void */
			}
			else {
				update_digit(0, decode7seg(g_hours_h));
				update_digit(1, decode7seg(g_hours_l));
				update_digit(2, decode7seg(g_minutes_h));
				update_digit(3, decode7seg(g_minutes_l));
			}
			break;
	}

	set_ramp(RAMP_MIN);
}


static void set_dots(int state)
{
	PORTB &= ~(!state << 7);
	PORTB |= !state << 7;
}


static void button_action(byte which)
{
	switch (g_mode) {
		case mode_calib:
			if (!which)
				calib_inc();
			else
				calib_dec();
			break;

		case mode_brightness:
			if (!which)
				brightness_inc();
			else
				brightness_dec();
			break;

		default:
			if (!which)
				minutes_inc();
			else
				hours_inc();
			g_seconds = 0;
			break;
	}

	g_mode_timeout = 0;
}


ISR(INT0_vect)
{
	byte update = 0, blanking = 0, btrigger = 0;

	wdt_reset();

	if (++g_subseconds >= RTC_HZ) {
		g_subseconds -= RTC_HZ;
		if (++g_seconds >= 60) {
			g_seconds = 0;
			minutes_inc();
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

	if ((btrigger = button_handle(0))) {
		button_action(0);
	}
	else if ((btrigger = button_handle(1))) {
		button_action(1);
	}
	else if (g_button_state[0] == button_longpress &&
			g_button_state[1] == button_longpress) {
		if (++g_mode == mode_end)
			g_mode = mode_normal;

		g_button_state[0] = g_button_state[1] = button_lockup;
		update = 1;
	}
	else if (g_mode == mode_normal &&
			!(g_subseconds % (RTC_HZ / LONGPRESS_HZ))) {
		btrigger = 1;
		if (g_button_state[0] == button_longpress)
			button_action(0);
		else if (g_button_state[1] == button_longpress)
			button_action(1);
		else
			btrigger = 0;
	}

	if (g_mode != mode_normal && ++g_mode_timeout > 5) {
		g_mode = mode_normal;
		update = 1;
	}

	if (btrigger) {
		update = 1;
		g_time_set = 1;
	}

	if (update)
		refresh_screen(blanking);
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
		set_ramp(g_rampcnt + RAMP_INC);
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
	refresh_screen(0);

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
	set_ramp(RAMP_MIN);
	set_brightness();
	TIMSK |= (1 << OCIE0B) | (1 << TOIE0) | (1 << OCIE0A);
	/* Enable counter (1/64 prescaler) */
	TCCR0B = (1 << CS01) | (1 << CS00);

	sleep_enable();
	sei();

	while (1)
		sleep_cpu();

	return 0;
}
