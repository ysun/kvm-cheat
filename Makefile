all: qemu-cheat img.bin

qemu-cheat: qemu-cheat.o qemu-cheat2.o
	gcc -g qemu-cheat2.c -o qemu-cheat2 -lpthread
	gcc -g qemu-cheat.c -o qemu-cheat

img.bin: img2.o img.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o img2.bin img.o
	ld -m elf_i386 --oformat binary -N -e _start -Ttext 0x10000 -o img.bin img.o

img.o: img.S img2.S
	as -32 img2.S -o img2.o
	as -32 img.S -o img.o
clean:
	rm -rf *.o qemu-cheat qemu-cheat2 img.bin img2.bin
