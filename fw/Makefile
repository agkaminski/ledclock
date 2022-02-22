ISP ?= usbasp
#ISP ?= avrisp2

all: ledclock.c
	avr-gcc -Os -mmcu=attiny2313 -Wall ledclock.c -o bin/ledclock
	avr-objcopy -Oihex bin/ledclock bin/ledclock.hex
	size -A -d bin/ledclock

fuse:
	avrdude -c${ISP} -pt2313 -U lfuse:w:0xe4:m

install:
	avrdude -c${ISP} -pt2313 -U flash:w:bin/ledclock.hex:i

clean:
	rm -f bin/*
