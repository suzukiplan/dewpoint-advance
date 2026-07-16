all:
	cd tools && make
	make -f Makefile.`uname` all

clean:
	make -f Makefile.`uname` clean
