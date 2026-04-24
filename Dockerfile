FROM ubuntu:noble

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Europe/Moscow

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    make \
    valgrind \
    graphviz \
    nasm \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /langroot
COPY . .

CMD ["/bin/bash"]