#include <klibc/kprint.h>

extern "C" {
    void kernel_main(void);
    void __cxa_pure_virtual() {};   // needed for pure virtual functions
}

void kernel_main(void){
    clearVGABuf();
    set_color_vga(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    puts_vga("This is a really long message to test the way the buffer works.");
}