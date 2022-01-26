all: ledclock.c
	avr-gcc -Os -mmcu=attiny2313 -Wall ledclock.c -o ledclock
	avr-objcopy -Oihex ledclock ledclock.hex
	size -A -d ledclock

clean:
	rm -f ledclock.hex ledclock
