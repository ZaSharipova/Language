CC = g++
CFLAGS = -ggdb3 -g -std=c++17 -O0 \
    -Iinclude -Iinclude/* -I. -Wall -Wextra -Weffc++ -Wc++14-compat -Wmissing-declarations \
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

OBJS_FRONT_NO_MAIN = $(filter-out build/front/main.o, $(OBJS_FRONT))

TARGET_FRONT  = build/bin/front
TARGET_MIDDLE = build/bin/middle  
TARGET_BACK   = build/bin/back
TARGET_ALL    = build/bin/solver

SRCS_FRONT  = $(wildcard Front-End/*.cpp)
SRCS_MIDDLE = $(wildcard Middle-End/*.cpp)
SRCS_BACK   = $(wildcard Back-End/*.cpp)
SRCS_COMMON = $(wildcard *.cpp)

OBJS_FRONT  = $(patsubst Front-End/%.cpp, build/front/%.o,  $(SRCS_FRONT))
OBJS_MIDDLE = $(patsubst Middle-End/%.cpp, build/middle/%.o, $(SRCS_MIDDLE))
OBJS_BACK   = $(patsubst Back-End/%.cpp, build/back/%.o,     $(SRCS_BACK))
OBJS_COMMON = $(patsubst %.cpp, build/common/%.o,            $(notdir $(SRCS_COMMON)))

OBJS_ALL = $(OBJS_FRONT) $(OBJS_MIDDLE) $(OBJS_BACK) $(OBJS_COMMON)

all: $(TARGET_ALL)
front: $(TARGET_FRONT)
middle: $(TARGET_MIDDLE)
back: $(TARGET_BACK)
solver: $(TARGET_ALL)

$(TARGET_FRONT): $(OBJS_FRONT) $(OBJS_COMMON)
	@mkdir -p build/bin
	@$(CC) $^ -o $@ $(LDFLAGS)

$(TARGET_MIDDLE): $(OBJS_MIDDLE) $(OBJS_COMMON) $(OBJS_FRONT_NO_MAIN)
	@mkdir -p build/bin
	@$(CC) $^ -o $@ $(LDFLAGS)

$(TARGET_BACK): $(OBJS_BACK) $(OBJS_COMMON) $(OBJS_FRONT) $(OBJS_MIDDLE)
	@mkdir -p build/bin
	@$(CC) $^ -o $@ $(LDFLAGS)

$(TARGET_ALL): $(OBJS_ALL)
	@mkdir -p build/bin
	@$(CC) $^ -o $@ $(LDFLAGS)

build/front/%.o: Front-End/%.cpp
	@mkdir -p build/front
	@$(CC) $(CFLAGS) -c $< -o $@

build/middle/%.o: Middle-End/%.cpp
	@mkdir -p build/middle
	$(CC) $(CFLAGS) -c $< -o $@

build/back/%.o: Back-End/%.cpp
	@mkdir -p build/back
	@$(CC) $(CFLAGS) -c $< -o $@

build/common/%.o: %.cpp
	@mkdir -p build/common
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf build

rebuild: clean all

debug: $(TARGET_ALL)
	./$(TARGET_ALL)

front-debug: $(TARGET_FRONT)
	./$(TARGET_FRONT)

middle-debug: $(TARGET_MIDDLE)
	./$(TARGET_MIDDLE)

back-debug: $(TARGET_BACK)
	./$(TARGET_BACK)

list:
	@echo "Доступные цели:"
	@echo "  make all|solver     - полная сборка"
	@echo "  make front          - фронтенд"
	@echo "  make middle         - мидл + фронтенд"
	@echo "  make back           - бэк + все предыдущие"

.PHONY: all front middle back solver clean rebuild debug front-debug middle-debug back-debug list
