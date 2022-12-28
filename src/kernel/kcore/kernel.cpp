#include <klibc/kprint.h>
extern "C" {
    void kernel_main(void);
    void __cxa_pure_virtual() {};   // needed for pure virtual functions
}

void kernel_main(void){
    clearVGABuf();
    set_color_vga(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    puts_vga("This is the OS entry point. Right now it's in VGA text mode. Before we continue with this OS, we need paging and an IDT.\n");
    puts_vga("Input is possible at this point, but it isn't as important as the other two. I'll probably work on it regardless. I will also write some rules for documentation.");
} 