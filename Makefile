AutoGenerates =\
	./src/dewpoint_define.h\
	./Makefile.Darwin

all:
	cd tools && make
	make ${AutoGenerates}
	./tools/makerom/makerom package.conf ./src/game_rom.c
	make -f Makefile.`uname` all

clean:
	make -f Makefile.`uname` clean

./src/dewpoint_define.h: ./src/dewpoint_define.h.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./Makefile.Darwin: ./Makefile.Darwin.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@
