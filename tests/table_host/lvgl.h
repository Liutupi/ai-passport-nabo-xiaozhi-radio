/* Minimal LVGL 9.5 font types for host-compiling the board's lv_font_conv output.
 * Only the data structures the board text renderers read; no LVGL code. */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LVGL_VERSION_MAJOR 9
#define LVGL_VERSION_MINOR 5
#define LV_VERSION_CHECK(x, y, z) ((x) < 9 || ((x) == 9 && (y) <= 5))
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_FONT_SUBPX_NONE 0

typedef struct lv_font_t lv_font_t;
#define LV_FONT_DECLARE(name) extern const lv_font_t name;

typedef struct {
    uint32_t bitmap_index : 20;
    uint32_t adv_w : 12;
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
} lv_font_fmt_txt_glyph_dsc_t;

typedef enum {
    LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL,
    LV_FONT_FMT_TXT_CMAP_SPARSE_FULL,
    LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY,
    LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
} lv_font_fmt_txt_cmap_type_t;

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t* unicode_list;
    const void* glyph_id_ofs_list;
    uint16_t list_length;
    lv_font_fmt_txt_cmap_type_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct {
    const uint8_t* glyph_bitmap;
    const lv_font_fmt_txt_glyph_dsc_t* glyph_dsc;
    const lv_font_fmt_txt_cmap_t* cmaps;
    const void* kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
} lv_font_fmt_txt_dsc_t;

struct lv_font_t {
    const void* get_glyph_dsc;
    const void* get_glyph_bitmap;
    int32_t line_height;
    int32_t base_line;
    uint8_t subpx;
    int8_t underline_position;
    int8_t underline_thickness;
    const void* dsc;
    const lv_font_t* fallback;
    void* user_data;
};

#define lv_font_get_glyph_dsc_fmt_txt NULL
#define lv_font_get_bitmap_fmt_txt NULL
