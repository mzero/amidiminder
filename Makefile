TARGET ?= amidiminder
PREFIX ?= /usr/local
BINARY_DIR ?= $(PREFIX)/bin
CONF_DIR ?= /etc
INSTALL ?= install
INSTALL_PROGRAM ?= $(INSTALL) -s
INSTALL_DATA ?= $(INSTALL) -m 644
MKDIR_P ?= mkdir -p

BUILD_DIR ?= ./build

all: bin
bin: $(BUILD_DIR)/$(TARGET)

deb:
	dpkg-buildpackage -b --no-sign

install:
	$(MKDIR_P) $(DESTDIR)$(BINARY_DIR) $(DESTDIR)$(CONF_DIR)
	$(INSTALL_PROGRAM) $(BUILD_DIR)/$(TARGET) $(DESTDIR)$(BINARY_DIR)/
	$(INSTALL_DATA) $(TARGET).rules $(DESTDIR)$(CONF_DIR)/

SRCS := amidiminder.cpp args.cpp files.cpp ipc.cpp rule.cpp seq.cpp
INCS :=
LIBS := stdc++ asound

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_FLAGS := $(addprefix -I,$(INCS))
CPPFLAGS += $(INC_FLAGS)
CPPFLAGS += -Wdate-time -D_FORTIFY_SOURCE=2
CPPFLAGS += -std=c++17
CPPFLAGS += -MMD -MP
CPPFLAGS += -O2
CPPFLAGS += -fstack-protector-strong -Wformat -Werror=format-security -Wall -pedantic

LDFLAGS += $(addprefix -l,$(LIBS))

ifeq ($(DEBUG),yes)
	CPPFLAGS += -DDEBUG -ggdb -O0
endif


# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) 2>&1 | tee $(BUILD_DIR)/link_out

.PHONY: clean test deb deb-clean

clean:
	$(RM) -r $(BUILD_DIR)

deb-clean:
	dh clean

test: $(BUILD_DIR)/$(TARGET)
	$(BUILD_DIR)/$(TARGET) check test.rules && echo PASS || echo FAIL


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


# dependencies

DEPS := $(OBJS:.o=.d)

-include $(DEPS)
