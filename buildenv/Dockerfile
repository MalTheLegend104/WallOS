FROM randomdude/gcc-cross-x86_64-elf

#ideally we should update to something newer than debain 9 with gcc 8, but it works and idrc
RUN echo "deb http://archive.debian.org/debian stretch main contrib non-free" > /etc/apt/sources.list
RUN apt-get update 
RUN apt-get upgrade -y
RUN apt-get install -y nasm
RUN apt-get install -y xorriso
RUN apt-get install -y grub-pc-bin
RUN apt-get install -y grub-common

VOLUME /root/env
WORKDIR /root/env

ENTRYPOINT [ "make", "build" ]