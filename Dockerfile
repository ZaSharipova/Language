# FROM ubuntu:noble

# ENV DEBIAN_FRONTEND=noninteractive
# ENV TZ=Europe/Moscow


# RUN apt update && apt install -y \
#     g++ \
#     make \
#     valgrind \
#     graphviz


# WORKDIR /langroot

# COPY . .

# RUN make clean && make front

# CMD ["./build/bin/front", "code-asm", "codeSquare.txt", "asm.asm"]

FROM ubuntu:22.04

RUN apt-get update --fix-missing

RUN apt-get install -y --no-install-recommends nasm
RUN apt-get install -y --no-install-recommends build-essential
RUN apt-get install -y --no-install-recommends gdb

RUN rm -rf /var/lib/apt/lists/*

WORKDIR /asm

CMD ["/bin/bash"]