sudo apt update
sudo apt upgrade -y
sudo apt install -y build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo xorriso grub-pc-bin grub-common nasm qemu qemu-system-x86 patch 

sudo chmod +xwr build build64 qemu clean

# Build GCC and Binutils
echo "Do you want to build GCC & Binutils (only recommended for first time setup)? [Y/n]"
read answer
if [[ $answer == y* ]]; then
	echo "Do you want to build a x86_64 cross-compiler? (default y) [Y/n]"
	read build64bit

	echo "Do you want to build a x86 cross-compiler? (default n) [Y/n]"
	read build32bit

	echo "Do you want to build a AArch64 cross-compiler? (default n) [Y/n]"
	read buildaarch

    echo -e "<------------Downloading GCC Binary----------->\n"
	mkdir ~/src/
	cd ~/src/
	wget -c https://ftp.gnu.org/gnu/gcc/gcc-10.2.0/gcc-10.2.0.tar.gz -O - | tar -xz

    echo -e "<---------Downloading Binutils Binary--------->\n"
	wget -c https://ftp.gnu.org/gnu/binutils/binutils-2.34.tar.gz -O - | tar -xz

	export PREFIX="$HOME/opt/cross"
	export TARGET=x86_64-elf
	export PATH="$PREFIX/bin:$PATH"

    
	if [[ $build64bit != n* ]]; then
		echo -e "<----------Building x86_64 Binutils----------->\n"
		cd $HOME/src

		mkdir build-binutils-x86_64
		cd build-binutils-x86_64
		../binutils-2.34/configure --target=x86_64-elf --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
		make -j 4
		make install
	fi

	if [[ $build32bit == y* ]]; then
		echo -e "<-----------Building x86 Binutils------------->\n"
		cd $HOME/src
		
		mkdir build-binutils-x86
		cd build-binutils-x86
		../binutils-2.34/configure --target=i686-elf --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
		make -j 4
		make install
	fi

	if [[ $buildaarch == y* ]]; then
		echo -e "<---------Building AArch64 Binutils----------->\n"
		cd $HOME/src
		
		mkdir build-binutils-aarch64
		cd build-binutils-aarch64
		../binutils-2.34/configure --target=aarch64-elf --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
		make -j 4
		make install
	fi


	echo -e "<---------------Patching Libgcc--------------->\n"
	cp "$(pwd)/src/t-x86_64-elf" ~/src/gcc-10.2.0/gcc/config/i386/
	cp "$(pwd)/src/config.gcc.patch" ~/src/gcc-10.2.0/gcc/

	cd ~/src/gcc-10.2.0/gcc
	patch < config.gcc.patch

	if [[ $build64bit != n* ]]; then
		echo -e "<-------------Building x86_64 GCC------------->\n"
		cd $HOME/src
		mkdir build-gcc-x86_64
		cd build-gcc-x86_64
		../gcc-10.2.0/configure --target=x86_64-elf --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
		make -j 4 all-gcc
		make -j 4 all-target-libgcc
		make install-gcc
		make install-target-libgcc
	fi
	
	if [[ $build32bit == y* ]]; then
		echo -e "<--------------Building x86 GCC--------------->\n"
		cd $HOME/src
		mkdir build-gcc-x86
		cd build-gcc-x86
		../gcc-10.2.0/configure --target=i686-elf --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
		make -j 4 all-gcc
		make -j 4 all-target-libgcc
		make install-gcc
		make install-target-libgcc
	fi

	if [[ $buildaarch == y* ]]; then
		echo -e "<------------Building AArch64 GCC------------->\n"
		cd $HOME/src
		mkdir build-gcc-aarch64
		cd build-gcc-aarch64
		../gcc-10.2.0/configure --target=aarch64-elf --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers
		make -j 4 all-gcc
		make -j 4 all-target-libgcc
		make install-gcc
		make install-target-libgcc
	fi

	cd $(pwd)
	export PATH="$HOME/opt/cross/bin:$PATH"
	echo "Do you want to add the cross compiler to the path (unless done before, say yes)? [Y/n]"
	read answer
	if [[ $answer != n* ]]; then
		sudo echo -e "\nexport PATH=\"\$HOME/opt/cross/bin:\$PATH\"" >> ~/.profile
	fi
fi