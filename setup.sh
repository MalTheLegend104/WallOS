# RUN THIS AS SUDO
apt-get update
apt-get upgrade -y
apt-get install -y xorriso grub-pc-bin grub-common nasm build-essential qemu docker
docker build buildenv -t wallos-buildenv

chmod +x build64 qemu clean