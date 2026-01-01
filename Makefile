CXX = g++

CXXFLAGS = -ggdb3 -g -std=c++17 -O0 \
	-Iinclude -I. -Wall -Wextra -Weffc++ -Wc++14-compat -Wmissing-declarations \
	-Wcast-align -Wcast-qual -Wchar-subscripts -Wconversion \
	-Wctor-dtor-privacy -Wempty-body -Wfloat-equal \
	-Wformat-nonliteral -Wformat-security -Wformat=2 \
  	-Winline -Wnon-virtual-dtor -Woverloaded-virtual \
   	-Wpacked -Wpointer-arith -Winit-self \
    -Wshadow -Wsign-conversion -Wsign-promo -Wstrict-overflow=2 \
	-Wsuggest-override -Wswitch-default -Wswitch-enum -Wundef \
	-Wunreachable-code -Wunused -Wvariadic-macros \
	-Wno-missing-field-initializers -Wno-narrowing -Wno-old-style-cast -Wno-varargs \
	-Wstack-protector -fcheck-new -fsized-deallocation -fstack-protector \
	-fstrict-overflow -fno-omit-frame-pointer -Wlarger-than=8192 \
	-fPIE -Werror=vla \
	-fsanitize=address,alignment,bool,bounds,enum,float-cast-overflow,float-divide-by-zero,integer-divide-by-zero,nonnull-attribute,null,return,returns-nonnull-attribute,shift,signed-integer-overflow,undefined,unreachable,vla-bound,vptr

LDFLAGS = -lm -fsanitize=address,alignment,bool,bounds,enum,float-cast-overflow,float-divide-by-zero,integer-divide-by-zero,nonnull-attribute,null,return,returns-nonnull-attribute,shift,signed-integer-overflow,undefined,unreachable,vla-bound,vptr

BUILD      = build
BIN        = $(BUILD)/bin
OBJ_COMMON = $(BUILD)/common
OBJ_FRONT  = $(BUILD)/front
OBJ_MIDDLE = $(BUILD)/middle
OBJ_BACK   = $(BUILD)/back
OBJ_REVERSE= $(BUILD)/reverse

COMMON_SRCS  = $(wildcard Common/*.cpp)
FRONT_SRCS   = $(wildcard Front-End/*.cpp)
MIDDLE_SRCS  = $(wildcard Middle-End/*.cpp)
BACK_SRCS    = $(wildcard Back-End/*.cpp)
REVERSE_SRCS = $(wildcard Reverse-End/*.cpp)

COMMON_OBJS  = $(COMMON_SRCS:Common/%.cpp=$(OBJ_COMMON)/%.o)
FRONT_OBJS   = $(FRONT_SRCS:Front-End/%.cpp=$(OBJ_FRONT)/%.o)
MIDDLE_OBJS  = $(MIDDLE_SRCS:Middle-End/%.cpp=$(OBJ_MIDDLE)/%.o)
BACK_OBJS    = $(BACK_SRCS:Back-End/%.cpp=$(OBJ_BACK)/%.o)
REVERSE_OBJS = $(REVERSE_SRCS:Reverse-End/%.cpp=$(OBJ_REVERSE)/%.o)
FRONT_OBJS_NO_MAIN = $(filter-out $(OBJ_FRONT)/main.o, $(FRONT_OBJS))

FRONT   = $(BIN)/front
MIDDLE  = $(BIN)/middle
BACK    = $(BIN)/back
REVERSE = $(BIN)/reverse
ALL     = $(BIN)/solver

all: $(ALL)
front: $(FRONT)
middle: $(MIDDLE)
back: $(BACK)
reverse: $(REVERSE)

$(FRONT): $(FRONT_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(MIDDLE): $(MIDDLE_OBJS) $(FRONT_OBJS_NO_MAIN) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(BACK): $(BACK_OBJS) $(FRONT_OBJS) $(MIDDLE_OBJS) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(REVERSE): $(REVERSE_OBJS) $(FRONT_OBJS_NO_MAIN) $(COMMON_OBJS)
	@mkdir -p $(BIN)
	@$(CXX) $^ -o $@ $(LDFLAGS)

$(ALL): $(FRONT_OBJS) $(MIDDLE_OBJS) $(BACK_OBJS) $(COMMON_OBJS)
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

clean:
	rm -rf $(BUILD)

rebuild: clean all

debug: all
	./$(ALL)

.PHONY: all front middle back reverse clean rebuild debug