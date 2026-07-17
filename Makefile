all:
	cd tools && make
	make ./src/dewpoint_define.h
	./tools/makerom/makerom package.conf ./src/game_rom.c
	make -f Makefile.`uname` all

./src/dewpoint_define.h: ./src/dewpoint_define.h.template ./package.conf
	./tools/conftype/conftype package.conf ./src/dewpoint_define.h.template >./src/dewpoint_define.h

clean:
	make -f Makefile.`uname` clean
