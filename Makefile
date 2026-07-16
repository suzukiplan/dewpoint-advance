all:
	cd tools && make
	./tools/makerom/makerom package.conf ./src/game_rom.c
	make -f Makefile.`uname` all

clean:
	make -f Makefile.`uname` clean
