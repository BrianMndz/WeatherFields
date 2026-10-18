# Upstream Provenance

| | Repository | Tag | Commit |
|---|---|---|---|
| u8g2 | https://github.com/olikraus/u8g2 | `2.37.1` | `414efcdb7a908667ea461c9e3a3a31da14fc4ea5` |

A curated copy, not a submodule: the upstream repository is over 400 MB
(tools, documentation, every font's sources), and WeatherFields needs 25 of
its files. Earlier copies came from Bela's OLED tutorial
(`github.com/giuliomoro/O2O`), an older u8g2 plus Linux I2C glue; that glue
is rewritten in `Platforms/Bela`, and nothing here comes from O2O.

## Policy

- `csrc/` holds the exact link closure of what WeatherFields uses: the u8g2
  drawing core, the u8x8 layer it sends through, and one display driver
  (`u8x8_d_ssd1306_128x64_noname.c`, which also holds the SH1106 128x64
  driver of 1.3" modules). Upstream layout and copyright headers are kept.
- Files are unchanged. A patch, if ever needed, goes in its own commit and
  is listed in a `PATCHES.md` here.
- Not imported: the other display drivers, `u8g2_d_setup.c` and
  `u8g2_d_memory.c` (every display's setup functions and buffers;
  `wf::oled` sets its display up with its own buffer), the font data
  (`u8g2_fonts.c`, `u8x8_fonts.c`; `wf::oled` compiles only the fonts it
  uses), MUI, u8log, the selection lists and the Arduino C++ wrapper
  (`cppsrc/`).
- Adding a file takes it from the same tag. Moving to a newer tag replaces
  every file together and rechecks the closure: every file listed in
  `CMakeLists.txt` must link with no other u8g2 file, as Bela's build links
  every object it compiles.

## Files

SHA-256 of each imported file, to recheck against the tag:

```text
d32d1b4d155e29b6bf7d5a4298da077649b43422f275261ff2ad2c5117183d2a  u8g2_arc.c
04a85f7ccc9e646a6bafe781c16cc22633d5d2bcd0bf13b18973d72afd56da5d  u8g2_bitmap.c
d329109913b00cfc42038cec55c07b0d8faf50e5995808fa0e0c132a5afd37c5  u8g2_box.c
9ca065a47345bb6bba3f7a81cad60ad7a15c3f2ca926137ad20f1e0ef1df4609  u8g2_buffer.c
6fbcc0dea8c64389ae6537f3e97722edfb85730480fe39747cf4aa1ebd717148  u8g2_circle.c
e1ca6ab2e977d9b1e6c72e020c80f6ea38dcab61eb9177e7b0b68ea5c6847c00  u8g2_cleardisplay.c
3d205ff43e0da767aaf87787777ef38ae930a876702c560e7d0e60183ad9730a  u8g2_font.c
e63dad17dfe6501fb4b8f47ea7daaaa75c720a88016cfa9af39cc17523fac808  u8g2_hvline.c
ed81a18801dfc27220f09162d8ec2a74cc76467bf672431dd6fac9bde66b46c4  u8g2_intersection.c
c249d6394de157f6c2914bfafcca9bbcd09197fdac96c36a1d9877c8155e5ccc  u8g2_kerning.c
a26d7b7ab8ebe3d48a318a93ae618b04b02623c8d86ee02d18ed47b1f4f0302e  u8g2_line.c
86fce4a252cbe32cdff4ac8bc694866136366d0311f8aaee1b0517a3c69b4ce8  u8g2_ll_hvline.c
2f94dc6f32e0434664de8b30b24dbf98a348230db369b807226311828f6e0249  u8g2_polygon.c
80bf40f40e35ab010d33ac8bebb9a7035c6293645f6f6b662a31a9fe52321d15  u8g2_setup.c
2d13834bb4449ba0c38756e9502d661e7bf3f63a244a9cc20270a9bd1d2fcc47  u8g2.h
da32dff05050e4b18ea9007fdefc49ef31bf24b85ba2200fa791755aa81b2884  u8x8_8x8.c
bb0f154d4fd7adf26b7f9f4188298d9d3e867627b6edc83385d64e26ec00cf23  u8x8_byte.c
3932b469ab8a27c330f52bd10abb2964600436495f320f93045d14dceddc91b7  u8x8_cad.c
36f3b20027f4dec2a48053e1f83034b559cd62723c74f3cabc956567de83d776  u8x8_capture.c
dcfaf1582aca38fff9799a592b895ca86680b1c0d34822074d24d2ea5c8cbeac  u8x8_d_ssd1306_128x64_noname.c
d04b8972f964401c447bf6105b9951f50cfe4b665248e9a4c875c5c32b910cdd  u8x8_display.c
884bc9f983276256faced6767a942ca5e79a03568b284165c0d1c4f855b0d630  u8x8_gpio.c
3f50da0b74fea01a3386a3192d412aceadc09cac3e5cbb0b879c1a7e80a38886  u8x8_setup.c
5a6877ecb5b0665a48f952588e5bdd173407cf4dac399bcfa75ba64dad412b42  u8x8_u16toa.c
c90875cf2ae48ffbe7932c10ea49d2a742c2a3a7a61b6b8d2df45a6721859ff9  u8x8.h
```
