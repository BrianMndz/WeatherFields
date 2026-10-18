# u8g2::u8g2: the curated u8g2 closure (UPSTREAM.md). Plain C with no
# hardware access of its own: a display reaches its panel only through the
# byte and GPIO callbacks its setup is given. No fonts and no display setup
# functions: wf::oled provides both.
add_library(u8g2 STATIC
    csrc/u8g2_arc.c
    csrc/u8g2_bitmap.c
    csrc/u8g2_box.c
    csrc/u8g2_buffer.c
    csrc/u8g2_circle.c
    csrc/u8g2_cleardisplay.c
    csrc/u8g2_font.c
    csrc/u8g2_hvline.c
    csrc/u8g2_intersection.c
    csrc/u8g2_kerning.c
    csrc/u8g2_line.c
    csrc/u8g2_ll_hvline.c
    csrc/u8g2_polygon.c
    csrc/u8g2_setup.c
    csrc/u8x8_8x8.c
    csrc/u8x8_byte.c
    csrc/u8x8_cad.c
    csrc/u8x8_capture.c
    csrc/u8x8_d_ssd1306_128x64_noname.c
    csrc/u8x8_display.c
    csrc/u8x8_gpio.c
    csrc/u8x8_setup.c
    csrc/u8x8_u16toa.c
)

add_library(u8g2::u8g2 ALIAS u8g2)

# gnu11 is what Bela's Makefile compiles C files with.
set_target_properties(u8g2 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON
)

target_include_directories(u8g2 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/csrc)
