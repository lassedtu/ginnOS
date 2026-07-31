#ifndef VGA_H
#define VGA_H

void vga_clear(void);
void vga_backspace(void);
void vga_write_char(char character);
void vga_write_line(const char *text);
void vga_write_string(const char *text);

#endif // VGA_H