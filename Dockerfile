FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow


RUN apt update && apt install -y \
    g++ \
    make \
    valgrind \
    graphviz


WORKDIR /langroot

COPY . .

RUN make clean && make front

CMD ["./build/bin/front", "code-asm", "codeSquare.txt", "asm.asm"]