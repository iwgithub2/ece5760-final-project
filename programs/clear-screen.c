#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "address_map_arm_brl4.h"

volatile unsigned int *vga_pixel_ptr;
void *vga_pixel_virtual_base;

#define VGA_BLACK 0x00

int main() {
    int fd;

    // Open physical memory
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        printf("ERROR: could not open /dev/mem\n");
        return 1;
    }

    // Map VGA pixel buffer
    vga_pixel_virtual_base = mmap(
        NULL,
        FPGA_ONCHIP_SPAN,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        SDRAM_BASE
    );

    if (vga_pixel_virtual_base == MAP_FAILED) {
        printf("ERROR: mmap failed\n");
        close(fd);
        return 1;
    }

    vga_pixel_ptr = (unsigned int *)vga_pixel_virtual_base;

    // Clear screen to black
    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            *((char*)vga_pixel_ptr + (y << 10) + x) = VGA_BLACK;
        }
    }

    printf("VGA cleared.\n");

    munmap(vga_pixel_virtual_base, FPGA_ONCHIP_SPAN);
    close(fd);

    return 0;
}
