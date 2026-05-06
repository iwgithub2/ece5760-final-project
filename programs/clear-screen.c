#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "address_map_arm_brl4.h"

#define VGA_BLACK 0x00

volatile unsigned int *vga_pixel_ptr = NULL;
volatile unsigned int *vga_char_ptr  = NULL;

void *vga_pixel_virtual_base = NULL;
void *vga_char_virtual_base  = NULL;

void VGA_pixel_clear(void) {
    if (!vga_pixel_ptr) return;

    for (int y = 0; y < 480; y++) {
        for (int x = 0; x < 640; x++) {
            *((char *)vga_pixel_ptr + (y << 10) + x) = VGA_BLACK;
        }
    }
}

void VGA_text_clear(void) {
    if (!vga_char_ptr) return;

    volatile char *character_buffer = (char *)vga_char_ptr;

    for (int y = 0; y < 60; y++) {
        for (int x = 0; x < 80; x++) {
            *(character_buffer + (y << 7) + x) = ' ';
        }
    }
}

int main(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        printf("ERROR: could not open /dev/mem\n");
        return 1;
    }

    vga_pixel_virtual_base = mmap(
        NULL,
        FPGA_ONCHIP_SPAN,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        SDRAM_BASE
    );

    if (vga_pixel_virtual_base == MAP_FAILED) {
        printf("ERROR: VGA pixel mmap failed\n");
        close(fd);
        return 1;
    }

    vga_pixel_ptr = (unsigned int *)vga_pixel_virtual_base;

    vga_char_virtual_base = mmap(
        NULL,
        FPGA_CHAR_SPAN,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        FPGA_CHAR_BASE
    );

    if (vga_char_virtual_base == MAP_FAILED) {
        printf("ERROR: VGA char mmap failed\n");
        munmap(vga_pixel_virtual_base, FPGA_ONCHIP_SPAN);
        close(fd);
        return 1;
    }

    vga_char_ptr = (unsigned int *)vga_char_virtual_base;

    VGA_pixel_clear();
    VGA_text_clear();

    printf("VGA pixel and text buffers cleared.\n");

    munmap(vga_pixel_virtual_base, FPGA_ONCHIP_SPAN);
    munmap(vga_char_virtual_base, FPGA_CHAR_SPAN);
    close(fd);

    return 0;
}
