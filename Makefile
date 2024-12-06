TARGET ?= amidiminder
PREFIX ?= /usr/local
BINARY_DIR ?= $(PREFIX)/bin
CONF_DIR ?= /etc
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -s
INSTALL_DATA ?= $(INSTALL) -m 644
MKDIR_P ?= mkdir -p

BUILD_DIR ?= ./build

all: bin format-man-pages
bin: $(BUILD_DIR)/$(TARGET)

deb:
	dpkg-buildpackage -b --no-sign

install:
	$(MKDIR_P) $(DESTDIR)$(BINARY_DIR)
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET) $(DESTDIR)$(BINARY_DIR)/

SRCS := amidiminder.cpp amidiminder-commands.cpp amidiminder-tests.cpp
SRCS +=	args.cpp client-list.cpp client-view.cpp files.cpp ipc.cpp main.cpp
SRCS += msg.cpp rule.cpp seq.cpp seqsnapshot.cpp term.cpp
INCS := .
LIBS := stdc++ asound fmt

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_FLAGS := $(addprefix -I,$(INCS))
CPPFLAGS += $(INC_FLAGS)
CPPFLAGS += -Wdate-time -D_FORTIFY_SOURCE=2
CPPFLAGS += -std=c++17
CPPFLAGS += -MMD -MP
CPPFLAGS += -O2
CPPFLAGS += -fstack-protector-strong -Wformat -Werror=format-security
CPPFLAGS += -Wall -Wextra -pedantic

LDFLAGS += $(addprefix -l,$(LIBS))


# c++ source
$(BUILD_DIR)/%.cpp.o: src/%.cpp
	$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) 2>&1 | tee $(BUILD_DIR)/link_out

.PHONY: clean test deb deb-clean tars

clean:
	$(RM) -r $(BUILD_DIR)

deb-clean:
	dh clean

test: $(BUILD_DIR)/$(TARGET)
	$(BUILD_DIR)/$(TARGET) check rules/test.rules && echo PASS || echo FAIL


# test shell with runtime and state directories in /tmp

TEST_DIR=/tmp/amidiminder-test
TEST_RUNTIME_DIR=$(TEST_DIR)/runtime
TEST_STATE_DIR=$(TEST_DIR)/state

$(BUILD_DIR)/test-env:
	echo PS1="'(amidiminder test): '" > $@
	echo export STATE_DIRECTORY=$(TEST_STATE_DIR) >> $@
	echo export RUNTIME_DIRECTORY=$(TEST_RUNTIME_DIR) >> $@

test-shell: $(BUILD_DIR)/test-env
	$(MKDIR_P) $(TEST_RUNTIME_DIR)
	$(MKDIR_P) $(TEST_STATE_DIR)
	$$SHELL --rcfile $(BUILD_DIR)/test-env || true


# man files

MANDIR ?= man
MANPAGES ?= amidiminder.1 amidiminder-profile.5 amidiminder-daemon.8
MANFILES ?= $(foreach page,$(MANPAGES),$(MANDIR)/$(page))
MANFORMATED ?= $(foreach file,$(MANFILES),$(file).txt)

$(MANFORMATED): %.txt : %
	groff -t -man -Tutf8 $< | col -b -x > $@

format-man-pages: $(MANFORMATED)


# tar files for Debian package

tars: deb-clean
	cd .. && tar -cJvf amidiminder_0.80.orig.tar.xz --exclude=debian amidiminder_0.80
	tar -cJvf ../amidiminder_0.80-1.debian.tar.xz debian/

# dependencies

DEPS := $(OBJS:.o=.d)

-include $(DEPS)
