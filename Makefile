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

SRCS := amidiminder.cpp args.cpp rule.cpp seq.cpp
INCS :=
LIBS := stdc++ asound

OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

INC_FLAGS := $(addprefix -I,$(INCS))
CPPFLAGS += $(INC_FLAGS)
CPPFLAGS += -MMD -MP
CPPFLAGS += -fdata-sections -ffunction-sections
CPPFLAGS += -O2

LDFLAGS += -Wl,--gc-sections $(addprefix -l,$(LIBS))

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
	$(BUILD_DIR)/$(TARGET) -C -f test.rules && echo PASS || echo FAIL

DEPS := $(OBJS:.o=.d)

-include $(DEPS)
