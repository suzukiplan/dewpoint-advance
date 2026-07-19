AutoGenerates =\
	./src/dewpoint_define.h\
	./vdf/action_manifest.vdf\
	./Makefile.Darwin\
	./Makefile.Linux

.PHONY: all test debug clean package

all:
	cd tools && make
	make ${AutoGenerates}
	./tools/makerom/makerom package.conf ./src/game_rom.c
	make -f Makefile.`uname` all

test:
	$(MAKE) -C tests test

debug: all
	make -f Makefile.`uname` debug

clean:
	$(MAKE) -C tests clean
	make -f Makefile.`uname` clean

package: all
	make -f Makefile.`uname` package

./src/dewpoint_define.h: ./src/dewpoint_define.h.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./vdf/action_manifest.vdf: ./vdf/action_manifest.vdf.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./Makefile.Darwin: ./Makefile.Darwin.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@

./Makefile.Linux: ./Makefile.Linux.template ./package.conf
	./tools/conftype/conftype package.conf $< >$@
