#
# Makefile for Image plugin to VDR
#
# (C) 2004-2011 Andreas Brachold    <vdr07 at deltab.de>
#     2013 Ulrich Eckhardt
#
# This code is distributed under the terms and conditions of the
# GNU GENERAL PUBLIC LICENSE. See the file COPYING for details.
#
#
# You can change the compile options here or create a Make.config
# in the VDR directory an set them there.
#


### uncomment the following line, if you don't have libexif installed
#WITHOUT_LIBEXIF=1


# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.
#
PLUGIN = image_next

#### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' image.c | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell pkg-config --variable=$(1) vdr || pkg-config --variable=$(1) ../../../vdr.pc))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
CFGDIR = $(call PKGCFG,configdir)/plugins/$(PLUGIN)
PLGCFG = $(call PKGCFG,plgcfg)
#
TMPDIR ?= /tmp

### The compiler options:

export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### The version number of VDR's plugin API (taken from VDR's "config.h"):

APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:

-include $(PLGCFG)

### The name of the distribution archive:

ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:

SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):
 
INCLUDES += -I$(VDRDIR)/include -I.
DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
CXXFLAGS +=-D__STDC_CONSTANT_MACROS

ifneq (exists, $(shell pkg-config libavcodec && echo exists))
  $(warning ******************************************************************)
  $(warning 'libavcodec' not detected!)
  $(warning ******************************************************************)
endif

ifneq (exists, $(shell pkg-config libavformat && echo exists))
  $(warning ******************************************************************)
  $(warning 'libavformat' not detected!)
  $(warning ******************************************************************)
endif

ifneq (exists, $(shell pkg-config libswscale && echo exists))
  $(warning ******************************************************************)
  $(warning 'libswscale' not detected!)
  $(warning ******************************************************************)
endif

ifneq (exists, $(shell pkg-config libexif && echo exists))
  $(warning ******************************************************************)
  $(warning 'libexif' not detected!)
  $(warning ******************************************************************)
  WITHOUT_LIBEXIF=1
endif

LIBS += $(shell pkg-config --libs libavformat)
CXXFLAGS += $(shell pkg-config --cflags libavformat)

LIBS += $(shell pkg-config --libs libavcodec)
CXXFLAGS += $(shell pkg-config --cflags libavcodec)

LIBS += $(shell pkg-config --libs libswscale)
CXXFLAGS += $(shell pkg-config --cflags libswscale)

# Skindesigner API
LIBS += $(shell pkg-config --libs libskindesignerapi 2>/dev/null || pkg-config --libs skindesignerapi 2>/dev/null || echo "-lskindesignerapi")
INCLUDES += $(shell pkg-config --cflags libskindesignerapi 2>/dev/null) -I/usr/include/libskindesignerapi

ifndef WITHOUT_LIBEXIF
  CXXFLAGS += $(shell pkg-config --cflags libexif)
  LIBS += $(shell pkg-config --libs libexif)
  DEFINES += -DHAVE_LIBEXIF
endif

### The object files (add further files here):

OBJS = image.o data.o menu.o data-image.o menu-image.o \
 setup-image.o player-image.o control-image.o commands.o menu-commands.o \
 list.o skindesigner_service.o \
 liboutput/encode.o liboutput/stillimage.o liboutput/stillimage-player.o \
 libimage/xpm.o


ifndef WITHOUT_LIBEXIF
  OBJS    += exif.o
endif

### The main target:

all: $(SOFILE) i18n

### Implicit rules:

%.o: %.c
	$(CXX) $(CXXFLAGS) $(CEXTRA) -c $(DEFINES) $(INCLUDES) -o $@ $<
	
### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.c) *.h > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	msgfmt -v -c -o $@ $<

$(I18Npot): $(wildcard *.c *.h)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) --msgid-bugs-address='<see README>' -o $@ `ls $^`

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

### Targets:

$(SOFILE): $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) $(LIBS) -o $@

install-lib: $(SOFILE)
	install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

dist: $(I18Npo) clean
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@cp -a * $(TMPDIR)/$(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)/.git
	@-rm -f $(TMPDIR)/$(ARCHIVE)/.gitignore
	@chown root.root -R $(TMPDIR)/$(ARCHIVE)/*
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz
	

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~ contrib/*~  examples/*~
