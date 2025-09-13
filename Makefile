include NickelHook/NickelHook.mk

override LIBRARY  := libtfpt.so
override SOURCES  += src/tfpt.cc
override CFLAGS   += -Wall -Wextra -Werror
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers
override LDFLAGS  += -lQt5Core
override PKGCONF  += Qt5Widgets

override MOCS += src/tfpt.h

include NickelHook/NickelHook.mk
