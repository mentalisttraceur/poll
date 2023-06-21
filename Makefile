default:
	gcc -std=c89 -pedantic \
	    -D_XOPEN_SOURCE \
	    -D_GNU_SOURCE \
	    -fPIE -Os -s -Wl,--gc-sections \
            -o poll poll.c
	strip -s poll

clean:
	rm -f poll
