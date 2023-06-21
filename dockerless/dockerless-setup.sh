sudo apt update
sudo apt upgrade -y
sudo apt install -y build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo xorriso grub-pc-bin grub-common nasm qemu qemu-system-x86 patch 

# Build GCC and Binutils
echo "Do you want to build GCC & Binutils (only recommended for first time setup)? [Y/n]"
read answer
if [[ $answer == y* ]]; then
    echo -e "<------------Downloading GCC Binary----------->\n"
	mkdir ~/src/
	cd ~/src/
	wget -c https://ftp.gnu.org/gnu/gcc/gcc-10.2.0/gcc-10.2.0.tar.gz -O - | tar -xz

    echo -e "<---------Downloading Binutils Binary--------->\n"
	wget -c https://ftp.gnu.org/gnu/binutils/binutils-2.34.tar.gz -O - | tar -xz

	export PREFIX="$HOME/opt/cross"
	export TARGET=x86_64-elf
	export PATH="$PREFIX/bin:$PATH"

    echo -e "<--------------Building Binutils-------------->\n"
	cd $HOME/src
	
	mkdir build-binutils
	cd build-binutils
	../binutils-2.34/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
	make -j 4
	make install

	echo -e "<---------------Patching Libgcc--------------->\n"
	cp "$(pwd)/src/t-x86_64-elf" ~/src/gcc-10.2.0/gcc/config/i386/
	cp "$(pwd)/src/config.gcc.patch" ~/src/gcc-10.2.0/gcc/

	cd ~/src/gcc-10.2.0/gcc
	patch < config.gcc.patch


	echo -e "<----------------Building GCC----------------->\n"
	cd $HOME/src
	mkdir build-gcc
	cd build-gcc
	../gcc-10.2.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
	make -j 4 all-gcc
	make -j 4 all-target-libgcc
	make install-gcc
	make install-target-libgcc
	cd $(pwd)

	echo "Do you want to add the cross compiler to the path (if no, you have to run `export PATH="$HOME/opt/cross/bin:$PATH"` every time you open a new terminal)? [Y/n]"
	read answer
	if [[ $answer == y* ]]; then
		sudo echo -e "\nexport PATH=\"\$HOME/opt/cross/bin:\$PATH\"" >> ~/.profile
	fi
fi


sudo chmod +xwr build build64 qemu