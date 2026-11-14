CC      = /opt/ppc-amigaos/bin/ppc-amigaos-gcc
STRIP   = /opt/ppc-amigaos/bin/ppc-amigaos-strip
SDK     = /opt/ppc-amigaos/ppc-amigaos/SDK

# Universal AmigaOS 4 PowerPC baseline (X5000, A1222+, Sam460, Sam440, G4, G3)
OPTIMIZE = -O2 -mcpu=powerpc \
           -fomit-frame-pointer -fno-strict-aliasing \
           -finline-functions -fweb -frename-registers -DNDEBUG

INCLUDES = -I./src \
           -I./amissl_sdk/include/include_h \
           -I$(SDK)/include/include_h \
           -I$(SDK)/include/netinclude \
           -I$(SDK)/local/common/include \
           -I$(SDK)/local/newlib/include

CFLAGS   = $(OPTIMIZE) -Wall -Wextra -Wno-deprecated-declarations \
           -finput-charset=UTF-8 -fexec-charset=ISO-8859-1 $(INCLUDES)

LDFLAGS  = -L$(SDK)/local/newlib/lib -lauto

SRCS = src/main.c \
       src/http_client.c \
       src/teletext.c \
       src/ttx_render.c \
       src/prefs.c \
       src/gui.c

OBJS = $(SRCS:.c=.o)

TARGET = VortexTeletext

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	$(STRIP) -s -R.comment -R.note $(TARGET) -o $(TARGET).stripped

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).stripped
