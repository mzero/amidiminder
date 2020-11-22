
TARGET ?= amidiminder

BUILD_DIR ?= ./build

all: bin deb
bin: $(BUILD_DIR)/$(TARGET)
deb: $(BUILD_DIR)/$(TARGET).deb

SRCS := amidiminder.cpp rule.cpp seq.cpp
INCS :=
LIBS := stdc++ asound


OBJS := $(SRCS:%=$(BUILD_DIR)/%.o)

MKDIR_P ?= mkdir -p

INC_FLAGS := $(addprefix -I,$(INCS))
CPPFLAGS := $(INC_FLAGS)
CPPFLAGS += -MMD -MP

LDFLAGS := -Wl,--gc-sections $(addprefix -l,$(LIBS))


# c++ source
$(BUILD_DIR)/%.cpp.o: %.cpp
	$(MKDIR_P) $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS) 2>&1 | tee $(BUILD_DIR)/link_out


.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)


DEPS := $(OBJS:.o=.d)

-include $(DEPS)

$(BUILD_DIR)/$(TARGET).deb: $(BUILD_DIR)/$(TARGET) $(TARGET).service debian/*
	rm -rf $(BUILD_DIR)/root
	mkdir -p $(BUILD_DIR)/root
	mkdir -p $(BUILD_DIR)/root/usr/bin
	cp -p $(BUILD_DIR)/$(TARGET) $(BUILD_DIR)/root/usr/bin
	mkdir -p $(BUILD_DIR)/root/usr/lib/systemd/system
	cp -p $(TARGET).service $(BUILD_DIR)/root/usr/lib/systemd/system
	cp -pr debian $(BUILD_DIR)/root/DEBIAN
	fakeroot dpkg --build $(BUILD_DIR)/root
	mv $(BUILD_DIR)/root.deb $(BUILD_DIR)/$(TARGET).deb


