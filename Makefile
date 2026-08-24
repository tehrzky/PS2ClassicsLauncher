# Package metadata
TITLE       := PS2 ISO Launcher
VERSION     := 01.00
TITLE_ID    := PSTL00001
CONTENT_ID  := IV0000-PSTL00001_00-PS2LAUNCHER00000

# Libraries - REMOVED lSceGpuAddress and lpng for now
LIBS        := -lc -lkernel -lm -lSceSystemService -lSceUserService -lScePad -lSceVideoOut -lSceGnmDriver -lSceLibcInternal -lSceSysmodule -lSceNet -lSceNetCtl -lSceHttp -lSceSsl -ljbc

# Toolchain
TOOLCHAIN   := $(OO_PS4_TOOLCHAIN)
INTDIR      := build
SRCDIR      := src
CDIR        := linux

# Compiler
CC          := clang
LD          := ld.lld

# Compiler flags
CFLAGS      := --target=x86_64-pc-freebsd12-elf -fPIC -funwind-tables -c -DORBIS -isysroot $(TOOLCHAIN) -isystem $(TOOLCHAIN)/include -I$(SRCDIR)

# Linker flags (with crt1.o for proper PS4 startup)
LDFLAGS     := -m elf_x86_64 -pie --script $(TOOLCHAIN)/link.x --eh-frame-hdr -L$(TOOLCHAIN)/lib $(LIBS) $(TOOLCHAIN)/lib/crt1.o

# Source files - ALL .c files in the src directory
CFILES      := $(wildcard $(SRCDIR)/*.c)
OBJS        := $(patsubst $(SRCDIR)/%.c,$(INTDIR)/%.o,$(CFILES))

all: $(CONTENT_ID).pkg

$(CONTENT_ID).pkg: pkg.gp4
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core pkg_build $< .

pkg.gp4: eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_module/libSceFios2.prx sce_module/libc.prx
	$(TOOLCHAIN)/bin/$(CDIR)/create-gp4 -out $@ --content-id=$(CONTENT_ID) --files "$^"

sce_sys/param.sfo: Makefile
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_new $@
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_TYPE --type Integer --maxsize 4 --value 1
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ APP_VER --type Utf8 --maxsize 8 --value '$(VERSION)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ ATTRIBUTE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CATEGORY --type Utf8 --maxsize 4 --value 'gd'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ CONTENT_ID --type Utf8 --maxsize 48 --value '$(CONTENT_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ SYSTEM_VER --type Integer --maxsize 4 --value 0
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE --type Utf8 --maxsize 128 --value '$(TITLE)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ TITLE_ID --type Utf8 --maxsize 12 --value '$(TITLE_ID)'
	$(TOOLCHAIN)/bin/$(CDIR)/PkgTool.Core sfo_setentry $@ VERSION --type Utf8 --maxsize 8 --value '$(VERSION)'

eboot.bin: $(INTDIR) $(OBJS)
	$(LD) $(OBJS) -o $(INTDIR)/ps2launcher.elf $(LDFLAGS)
	$(TOOLCHAIN)/bin/$(CDIR)/create-fself -in=$(INTDIR)/ps2launcher.elf -out=$(INTDIR)/ps2launcher.oelf --eboot "eboot.bin" --paid 0x3800000000000011 --authinfo 000000000000000000000000001C004000FF000000000080000000000000000000000000000000000000008000400040000000000000008000000000000000080040FFFF000000F000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000

$(INTDIR)/%.o: $(SRCDIR)/%.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(INTDIR):
	mkdir -p $(INTDIR)

clean:
	rm -f $(CONTENT_ID).pkg pkg.gp4 sce_sys/param.sfo eboot.bin \
	      $(INTDIR)/ps2launcher.elf $(INTDIR)/ps2launcher.oelf $(OBJS)

.PHONY: all clean
