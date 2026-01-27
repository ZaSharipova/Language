
CXX = g++
MODE ?= debug

UNAME_S := $(shell uname -s)

SANITIZERS =

ifeq ($(MODE),debug)
	SANITIZERS = -fsanitize=address,undefined
endif

CXXFLAGS = -ggdb3 -g -std=c++17 -O0 \
	-Iinclude -I. \
	-Wall -Wextra -Weffc++ -Wc++14-compat -Wmissing-declarations \
	-Wcast-align -Wcast-qual -Wchar-subscripts -Wconversion \
	-Wctor-dtor-privacy -Wempty-body -Wfloat-equal \
	-Wformat-nonliteral -Wformat-security -Wformat=2 \
	-Winline -Wnon-virtual-dtor -Woverloaded-virtual \
	-Wpacked -Wpointer-arith -Winit-self \
	-Wshadow -Wsign-conversion -Wsign-promo -Wstrict-overflow=2 \
	-Wsuggest-override -Wswitch-default -Wswitch-enum -Wundef \
	-Wunreachable-code -Wunused -Wvariadic-macros \
	-Wno-missing-field-initializers -Wno-narrowing \
	-Wno-old-style-cast -Wno-varargs \
	-Wstack-protector -fcheck-new -fsized-deallocation \
	-fstack-protector -fstrict-overflow -fno-omit-frame-pointer \
	-Wlarger-than=8192 -fPIE -Werror=vla \
	$(SANITIZERS)

LDFLAGS = -lm $(SANITIZERS)

BUILD       = build
BIN         = $(BUILD)/bin
OBJ_COMMON  = $(BUILD)/common
OBJ_FRONT   = $(BUILD)/front
OBJ_MIDDLE  = $(BUILD)/middle
OBJ_BACK    = $(BUILD)/back
OBJ_REVERSE = $(BUILD)/reverse
OBJ_TRICK   = $(BUILD)/trick

COMMON_SRCS  = $(wildcard Common/*.cpp)
FRONT_SRCS   = $(wildcard Front-End/*.cpp)
MIDDLE_SRCS  = $(wildcard Middle-End/*.cpp)
BACK_SRCS    = $(wildcard Back-End/*.cpp)
REVERSE_SRCS = $(wildcard Reverse-End/*.cpp)
TRICK_SRCS   = $(wildcard Trick-End/*.cpp)

COMMON_OBJS  = $(COMMON_SRCS:Common/%.cpp=$(OBJ_COMMON)/%.o)
FRONT_OBJS   = $(FRONT_SRCS:Front-End/%.cpp=$(OBJ_FRONT)/%.o)
MIDDLE_OBJS  = $(MIDDLE_SRCS:Middle-End/%.cpp=$(OBJ_MIDDLE)/%.o)
BACK_OBJS    = $(BACK_SRCS:Back-End/%.cpp=$(OBJ_BACK)/%.o)
REVERSE_OBJS = $(REVERSE_SRCS:Reverse-End/%.cpp=$(OBJ_REVERSE)/%.o)
RULES_OBJ    = $(OBJ_FRONT)/Rules.o $(OBJ_FRONT)/LexicalAnalysis.o 
TRICK_OBJS  = $(TRICK_SRCS:Trick-End/%.cpp=$(OBJ_TRICK)/%.o) $(RULES_OBJ)

FRONT_OBJS_NO_MAIN   = $(filter-out $(OBJ_FRONT)/main.o, $(FRONT_OBJS))
MIDDLE_OBJS_NO_MAIN  = $(filter-out $(OBJ_MIDDLE)/main.o, $(MIDDLE_OBJS))
BACK_OBJS_NO_MAIN    = $(filter-out $(OBJ_BACK)/main.o, $(BACK_OBJS))
REVERSE_OBJS_NO_MAIN = $(filter-out $(OBJ_REVERSE)/main.o, $(REVERSE_OBJS))
TRICK_OBJS_NO_MAIN   = $(filter-out $(OBJ_TRICK)/main.o, $(TRICK_OBJS))

FRONT   = $(BIN)/front
MIDDLE  = $(BIN)/middle
BACK    = $(BIN)/back
REVERSE = $(BIN)/reverse
TRICK   = $(BIN)/trick

all: front middle back reverse

front: $(FRONT)
middle: $(MIDDLE)
back: $(BACK)
reverse: $(REVERSE)
trick: $(TRICK)

$(FRONT): $(FRONT_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(MIDDLE): $(MIDDLE_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(BACK): $(BACK_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(REVERSE): $(REVERSE_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(TRICK): $(TRICK_OBJS) $(COMMON_OBJS) $()
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(OBJ_COMMON)/%.o: Common/%.cpp
	@mkdir -p $(OBJ_COMMON)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_FRONT)/%.o: Front-End/%.cpp
	@mkdir -p $(OBJ_FRONT)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_MIDDLE)/%.o: Middle-End/%.cpp
	@mkdir -p $(OBJ_MIDDLE)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_BACK)/%.o: Back-End/%.cpp
	@mkdir -p $(OBJ_BACK)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_REVERSE)/%.o: Reverse-End/%.cpp
	@mkdir -p $(OBJ_REVERSE)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_TRICK)/%.o: Trick-End/%.cpp
	@mkdir -p $(OBJ_TRICK)
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)

rebuild: clean all

debug: all
	./$(REVERSE)

leaks-front:
	$(MAKE) MODE=leak clean front
	leaks --atExit -- ./$(FRONT)

leaks-middle:
	$(MAKE) MODE=leak clean middle
	leaks --atExit -- ./$(MIDDLE)

leaks-back:
	$(MAKE) MODE=leak clean back
	leaks --atExit -- ./$(BACK)

leaks-reverse:
	$(MAKE) MODE=leak clean reverse
	leaks --atExit -- ./$(REVERSE)

.PHONY: all front middle back reverse \
        clean rebuild debug \
        leaks-front leaks-middle leaks-back leaks-reverse
