# !/usr/bin/env sh

docker build --platform linux/amd64 -t lang .          
docker run --cap-add=SYS_PTRACE --security-opt seccomp=unconfined -it --platform linux/amd64 \
    -v "$(pwd)":/lang \
    lang

ROSETTA_DEBUGSERVER_PORT=1234 ./program & gdb

#inside gdb
set architecture i386:x86-64
file ./program
target remote localhost:1234
