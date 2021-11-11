# dwm version
VERSION = 0.6.2

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

X11INC = /usr/X11R6/include
X11LIB = /usr/X11R6/lib

# Xinerama, comment if you don't want it
XINERAMALIBS  = -lXinerama
XINERAMAFLAGS = -DXINERAMA

# freetype
FREETYPELIBS = -lfontconfig -lXft
FREETYPEINC = /usr/include/freetype2

# includes and libs
INCS = -I${X11INC} -I${FREETYPEINC}
LIBS = -lstdc++ -L${X11LIB} -lX11 ${XINERAMALIBS} ${FREETYPELIBS}

# flags
CXXFLAGS = -std=c++20 -Wpedantic -Wall -Wextra -Wno-deprecated-declarations ${INCS} -D_DEFAULT_SOURCE -DVERSION=\"${VERSION}\" ${XINERAMAFLAGS}
LDFLAGS  = ${LIBS}

DEBUG_CXXFLAGS = -fsanitize=address,undefined -g -Og
DEBUG_LDFLAGS = -fsanitize=address,undefined

RELEASE_CXXFLAGS = -O3
RELEASE_LDFLAGS = 

# compiler and linker
CXX = c++
