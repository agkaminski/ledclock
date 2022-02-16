all: ledclock.c
	avr-gcc -Os -mmcu=attiny2313 -Wall ledclock.c -o ledclock
	avr-objcopy -Oihex ledclock ledclock.hex
	size -A -d ledclock

fuse:
	avrdude -cusbasp -pt2313 -U lfuse:w:0xe4:m

install:
	avrdude -cusbasp -pt2313 -U flash:w:ledclock.hex:i

clean:
	rm -f ledclock.hex ledclock
