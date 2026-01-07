[private]
default:
    @just --list --unsorted

cxx := "g++"
build_dir := "build"
bin_dir := build_dir / "bin"

mode := env_var_or_default("MODE", "debug")

base_cxxflags := "-ggdb3 -g -std=c++17 -O0 \
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
	-Wlarger-than=8192 -fPIE -Werror=vla"

sanitizers_flag := if mode == "debug" {
    "-fsanitize=address,undefined"
} else {
    ""
}

[group("FrontEnd")]
front-build:
    @mkdir -p {{bin_dir}}
    @{{cxx}} {{base_cxxflags}} {{sanitizers_flag}} Front-End/*.cpp Common/*.cpp -o {{bin_dir}}/front -lm

[group("FrontEnd")]
front-run *ARGS:
    {{bin_dir}}/front {{ARGS}}

[group("MiddleEnd")]
middle-build:
    @mkdir -p {{bin_dir}}
    @{{cxx}} {{base_cxxflags}} {{sanitizers_flag}} Middle-End/*.cpp Common/*.cpp -o {{bin_dir}}/middle -lm

[group("MiddleEnd")]
middle-run *ARGS:
    {{bin_dir}}/middle {{ARGS}}

[group("BackEnd")]
back-build:
    @mkdir -p {{bin_dir}}
    @{{cxx}} {{base_cxxflags}} {{sanitizers_flag}} Back-End/*.cpp Common/*.cpp -o {{bin_dir}}/back -lm

[group("BackEnd")]
back-run *ARGS:
    {{bin_dir}}/back {{ARGS}}

[group("ReverseEnd")]
reverse-build:
    @mkdir -p {{bin_dir}}
    @{{cxx}} {{base_cxxflags}} {{sanitizers_flag}} Reverse-End/*.cpp Common/*.cpp -o {{bin_dir}}/reverse -lm

[group("ReverseEnd")]
reverse-run *ARGS:
    {{bin_dir}}/reverse {{ARGS}}

[group("All")]
all-build:
    just front-build
    just middle-build
    just back-build
    just reverse-build

[group("All")]
all-build-release:
    MODE=release just front-build
    MODE=release just middle-build
    MODE=release just back-build
    MODE=release just reverse-build

clean:
    rm -rf {{build_dir}}

rebuild: clean all-build

debug:
    just reverse-run

[private]
show-mode:
    @echo "Current mode: {{mode}}"
    @echo "Sanitizers flag: {{sanitizers_flag}}"

[private]
set-debug:
    @echo "Export MODE=debug"
    export MODE=debug

[private]
set-release:
    @echo "Export MODE=release"
    export MODE=release