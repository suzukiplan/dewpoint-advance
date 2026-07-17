AutoGenerates =\
	./src/dewpoint_define.h\
	./vdf/action_manifest.vdf\
	./Makefile.Darwin

all:
	cd tools && make
	make ${AutoGenerates}
	./tools/makerom/makerom package.conf ./src/game_rom.c
	make -f Makefile.`uname` all

debug: all
	make -f Makefile.`uname` debug

clean:
	make -f Makefile.`uname` clean

package: all
	make -f Makefile.`uname` package

./src/dewpoint_define.h: ./src/dewpoint_define.h.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./vdf/action_manifest.vdf: ./vdf/action_manifest.vdf.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./Makefile.Darwin: ./Makefile.Darwin.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@
