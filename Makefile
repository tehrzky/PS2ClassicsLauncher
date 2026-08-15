TARGET      := ps2launcher
OUTDIR      := .
INTDIR      := build
PKGDIR      := pkg

CC          := clang
LD          := ld.lld
OBJCOPY     := llvm-objcopy

INCLUDES    := -I$(OO_PS4_TOOLCHAIN)/include -I$(OO_PS4_TOOLCHAIN)/include/c++/v1
LIBDIRS     := -L$(OO_PS4_TOOLCHAIN)/lib

LIBS        := -lSceSystemService -lSceUserService -lScePad -lSceVideoOut -lSceGnmDriver -lSceLibcInternal -lkernel

CFLAGS      := -cc1 -triple x86_64-scei-ps4-elf $(INCLUDES) -DORBIS -emit-obj
LDFLAGS     := -m elf_x86_64 -pie --script $(OO_PS4_TOOLCHAIN)/link.x --eh-frame-hdr $(LIBDIRS) $(LIBS)

CFILES      := main.c
OBJS        := $(patsubst %.c,$(INTDIR)/%.o,$(notdir $(CFILES)))

all: pkg

$(INTDIR)/%.o: %.c | $(INTDIR)
	$(CC) $(CFLAGS) -o $@ $<

$(OUTDIR)/$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $<
	$(OBJCOPY) --only-keep-debug $@ $(OUTDIR)/$(TARGET).elf.debug
	$(OBJCOPY) --strip-debug $@

$(PKGDIR)/eboot.bin: $(OUTDIR)/$(TARGET).elf
	python3 $(OO_PS4_TOOLCHAIN)/bin/linux/create-fself.py -in $< -out $@ --eboot

$(OUTDIR)/$(TARGET).pkg: $(PKGDIR)/eboot.bin $(PKGDIR)/pkg.gp4 sce_sys/param.sfo sce_sys/icon0.png
	$(OO_PS4_TOOLCHAIN)/bin/linux/PkgTool.Core pkg_build $(PKGDIR)/pkg.gp4 $(OUTDIR)

pkg: $(OUTDIR)/$(TARGET).pkg

$(INTDIR):
	mkdir -p $(INTDIR)

clean:
	rm -rf $(INTDIR) $(OUTDIR)/$(TARGET).elf $(OUTDIR)/$(TARGET).pkg $(OUTDIR)/$(TARGET).elf.debug $(PKGDIR)/eboot.bin

.PHONY: all clean pkg
