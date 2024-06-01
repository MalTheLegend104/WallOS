# RUN THIS AS SUDO
sudo apt update
sudo apt upgrade -y
sudo apt-get install -y docker xorriso grub-pc-bin grub-common nasm build-essential qemu

sudo docker build buildenv -t wallos-buildenv

sudo chmod +x build64 qemu clean