FILES = ./build/kernel.asm.o ./build/isr.o ./build/kernel.o ./build/vga.o ./build/panic.o ./build/idt.o ./build/pic.o ./build/keyboard.o ./build/printk.o ./build/shell.o
FLAGS =  -g -ffreestanding -nostdlib -nostartfiles -nodefaultlibs -Wall -O0 -Iinc

all:
	nasm -f bin ./src/boot.asm -o ./bin/boot.bin
	nasm -f elf -g ./src/kernel.asm -o ./build/kernel.asm.o
	nasm -f elf -g ./src/isr.asm -o ./build/isr.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/kernel.c -o ./build/kernel.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/vga.c -o ./build/vga.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/panic.c -o ./build/panic.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/idt.c -o ./build/idt.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/pic.c -o ./build/pic.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/keyboard.c -o ./build/keyboard.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/printk.c -o ./build/printk.o
	i686-elf-gcc -I./src $(FLAGS) -std=gnu99 -c ./src/shell.c -o ./build/shell.o
	i686-elf-ld -g -relocatable $(FILES) -o ./build/completeKernel.o
	i686-elf-gcc $(FLAGS) -T ./linkerScript.ld -o ./bin/kernel.bin -ffreestanding -O0 -nostdlib ./build/completeKernel.o

	dd if=./bin/boot.bin >> ./bin/os.bin
	dd if=./bin/kernel.bin >> ./bin/os.bin
	dd if=/dev/zero bs=512 count=8 >> ./bin/os.bin

clean:
	rm -f ./bin/boot.bin
	rm -f ./bin/kernel.bin
	rm -f ./bin/os.bin
	rm -f ./build/kernel.asm.o
	rm -f ./build/isr.o
	rm -f ./build/kernel.o
	rm -f ./build/vga.o
	rm -f ./build/panic.o
	rm -f ./build/idt.o
	rm -f ./build/pic.o
	rm -f ./build/keyboard.o
	rm -f ./build/printk.o
	rm -f ./build/shell.o
	rm -f ./build/completeKernel.o