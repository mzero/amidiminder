TARGET_SERVER ?= midiminder
TARGET_USER ?= midiwala
PREFIX ?= /usr/local
BINARY_DIR ?= $(PREFIX)/bin
CONF_DIR ?= /etc
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -s
INSTALL_DATA ?= $(INSTALL) -m 644
MKDIR_P ?= mkdir -p

BUILD_DIR ?= ./build

all: bin format-man-pages
bin: $(BUILD_DIR)/$(TARGET_SERVER) $(BUILD_DIR)/$(TARGET_USER)

deb:
	dpkg-buildpackage -b --no-sign
deb-nc:
	dpkg-buildpackage -nc -b --no-sign

install:
	$(MKDIR_P) $(DESTDIR)$(BINARY_DIR)
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET_SERVER) $(DESTDIR)$(BINARY_DIR)/
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET_USER) $(DESTDIR)$(BINARY_DIR)/

SRCS_COMMON := msg.cpp rule.cpp seq.cpp

SRCS_SERVER := service.cpp service-commands.cpp service-tests.cpp
SRCS_SERVER +=	args-service.cpp main-service.cpp
SRCS_SERVER += files.cpp ipc.cpp
SRCS_SERVER += $(SRCS_COMMON)

SRCS_USER += user-connect.cpp user-list.cpp user-view.cpp
SRCS_USER += args-user.cpp main-user.cpp
SRCS_USER += seqsnapshot.cpp term.cpp
SRCS_USER += $(SRCS_COMMON)


INCS := .
LIBS := stdc++ asound fmt

OBJS_SERVER := $(SRCS_SERVER:%=$(BUILD_DIR)/%.o)
OBJS_USER := $(SRCS_USER:%=$(BUILD_DIR)/%.o)

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
	@$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET_SERVER): $(OBJS_SERVER)
	$(CC) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(TARGET_USER): $(OBJS_USER)
	$(CC) $^ -o $@ $(LDFLAGS)


.PHONY: clean test deb deb-clean tars

clean:
	$(RM) -r $(BUILD_DIR)

deb-clean:
	dh clean

test: $(BUILD_DIR)/$(TARGET_SERVER)
	$(BUILD_DIR)/$(TARGET_SERVER) check rules/test.rules && echo PASS || echo FAIL


# test shell with runtime and state directories in /tmp

TEST_DIR=/tmp/midiminder-test
TEST_RUNTIME_DIR=$(TEST_DIR)/runtime
TEST_STATE_DIR=$(TEST_DIR)/state

$(BUILD_DIR)/test-env:
	echo PS1="'(midiminder test): '" > $@
	echo export STATE_DIRECTORY=$(TEST_STATE_DIR) >> $@
	echo export RUNTIME_DIRECTORY=$(TEST_RUNTIME_DIR) >> $@

test-shell: $(BUILD_DIR)/test-env
	$(MKDIR_P) $(TEST_RUNTIME_DIR)
	$(MKDIR_P) $(TEST_STATE_DIR)
	$$SHELL --rcfile $(BUILD_DIR)/test-env || true


# man files

MANDIR ?= man
MANPAGES ?= midiminder.1 midiminder-profile.5 midiminder-daemon.8 midiwala.1
MANFILES ?= $(foreach page,$(MANPAGES),$(MANDIR)/$(page))
MANFORMATED ?= $(foreach file,$(MANFILES),$(file).txt)

$(MANFORMATED): %.txt : %
	groff -t -man -Tutf8 $< | col -b -x > $@

format-man-pages: $(MANFORMATED)


# tar files for Debian package

tars: deb-clean
	cd .. && tar -cJvf midiminder_1.0.orig.tar.xz --exclude=debian midiminder_1.0
	tar -cJvf ../midiminder_1.0-1.debian.tar.xz debian/

# dependencies

DEPS := $(OBJS_SERVER:.o=.d) $(OBJS_USER:.o=.d)

-include $(DEPS)
