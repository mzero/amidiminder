
TARGET ?= amidiminder

BUILD_DIR ?= ./build

SRCS := amidiminder.cpp
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

