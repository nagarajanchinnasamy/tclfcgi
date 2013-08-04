PACKAGE=Fcgi
LIB=libtclfcgi
LIB_VERSION=0.8
LIB_INSTALL_DIR=/usr/lib/tcltk/tclfcgi$(LIB_VERSION)
TCL_LIB=tcl8.5
FCGI_LIB=fcgi
all:
	gcc -fPIC -c -Wall -I/usr/local/include tclFcgiCmd.c
	gcc -shared -Wl,-soname,$(LIB).so. -o $(LIB)$(LIB_VERSION).so tclFcgiCmd.o -l$(TCL_LIB) -l$(FCGI_LIB)
	echo "package ifneeded $(PACKAGE) $(LIB_VERSION) \"load [file join \$$dir $(LIB)$(LIB_VERSION).so]\"" >pkgIndex.tcl
install:
	mkdir -p $(LIB_INSTALL_DIR)
	cp $(LIB)$(LIB_VERSION).so pkgIndex.tcl $(LIB_INSTALL_DIR)
clean:
	rm -f tclFcgiCmd.o $(LIB)$(LIB_VERSION).so pkgIndex.tcl
