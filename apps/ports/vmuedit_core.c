/* VMU Editor core. GPL-2.0-or-later. See vmuedit_core.h. */
#include "vmuedit_core.h"
#include <string.h>

/* ---- LCD bitmap ---------------------------------------------------------------- */
int lcd_get(const uint8_t *lcd, int x, int y)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H)
        return 0;
    return (lcd[y * 6 + x / 8] >> (7 - (x & 7))) & 1;
}
void lcd_put(uint8_t *lcd, int x, int y, int dark)
{
    if (x < 0 || y < 0 || x >= LCD_W || y >= LCD_H)
        return;
    uint8_t mask = 0x80 >> (x & 7);
    if (dark)
        lcd[y * 6 + x / 8] |= mask;
    else
        lcd[y * 6 + x / 8] &= ~mask;
}
void lcd_invert(uint8_t *lcd)
{
    for (int i = 0; i < LCD_BYTES; i++)
        lcd[i] ^= 0xff;
}
void lcd_clear(uint8_t *lcd)
{
    memset(lcd, 0, LCD_BYTES);
}
void lcd_flip_h(uint8_t *lcd)
{
    uint8_t out[LCD_BYTES] = {0};
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            lcd_put(out, LCD_W - 1 - x, y, lcd_get(lcd, x, y));
    memcpy(lcd, out, LCD_BYTES);
}
void lcd_flip_v(uint8_t *lcd)
{
    uint8_t out[LCD_BYTES];
    for (int y = 0; y < LCD_H; y++)
        memcpy(out + (LCD_H - 1 - y) * 6, lcd + y * 6, 6);
    memcpy(lcd, out, LCD_BYTES);
}
void lcd_shift(uint8_t *lcd, int dx, int dy)
{
    uint8_t out[LCD_BYTES] = {0};
    for (int y = 0; y < LCD_H; y++)
        for (int x = 0; x < LCD_W; x++)
            lcd_put(out, ((x + dx) % LCD_W + LCD_W) % LCD_W, ((y + dy) % LCD_H + LCD_H) % LCD_H,
                    lcd_get(lcd, x, y));
    memcpy(lcd, out, LCD_BYTES);
}

/* ---- BMP ----------------------------------------------------------------------- */
static void put16(uint8_t *p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}
static void put32(uint8_t *p, unsigned v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}
static unsigned get16(const uint8_t *p)
{
    return p[0] | (unsigned)p[1] << 8;
}
static uint32_t get32(const uint8_t *p)
{
    return get16(p) | (uint32_t)get16(p + 2) << 16;
}
size_t lcd_to_bmp(const uint8_t *lcd, uint8_t *out)
{
    memset(out, 0, LCD_BMP_BYTES);
    out[0] = 'B';
    out[1] = 'M';
    put32(out + 2, LCD_BMP_BYTES);
    put32(out + 10, 62);
    put32(out + 14, 40);
    put32(out + 18, LCD_W);
    put32(out + 22, LCD_H);
    put16(out + 26, 1);
    put16(out + 28, 1);
    put32(out + 34, 256);
    put32(out + 46, 2);
    put32(out + 50, 2);
    memset(out + 54, 0xff, 3); /* index 0: white (light pixel) */
    /* index 1: black (dark pixel) is all zero */
    for (int y = 0; y < LCD_H; y++)
        memcpy(out + 62 + 8 * (LCD_H - 1 - y), lcd + y * 6, 6); /* bottom-up, padded to 8 */
    return LCD_BMP_BYTES;
}
const char *bmp_error_text(int error)
{
    switch (error) {
    case BMP_TRUNCATED: return "The BMP file is truncated or damaged.";
    case BMP_NOT_BMP: return "This is not a BMP file.";
    case BMP_UNSUPPORTED: return "Use an uncompressed 1, 4, 8, 24 or 32-bit BMP.";
    case BMP_TOO_LARGE: return "The BMP is larger than 4096 x 4096.";
    default: return "";
    }
}
int lcd_from_bmp(const uint8_t *d, size_t n, uint8_t *lcd, int *adjusted)
{
    if (n < 2 || d[0] != 'B' || d[1] != 'M')
        return BMP_NOT_BMP;
    if (n < 54)
        return BMP_TRUNCATED;
    uint32_t offset = get32(d + 10), header = get32(d + 14);
    int32_t w = (int32_t)get32(d + 18), h = (int32_t)get32(d + 22);
    unsigned planes = get16(d + 26), bpp = get16(d + 28);
    uint32_t compression = get32(d + 30), used = get32(d + 46);
    if (header < 40 || header > 200)
        return BMP_UNSUPPORTED;
    int top_down = h < 0;
    if (top_down)
        h = -h;
    if (planes != 1 || compression != 0 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32))
        return BMP_UNSUPPORTED;
    if (w <= 0 || h <= 0)
        return BMP_UNSUPPORTED;
    if (w > 4096 || h > 4096)
        return BMP_TOO_LARGE;
    uint32_t colours = 0;
    const uint8_t *palette = d + 14 + header;
    if (bpp <= 8) {
        colours = used ? used : 1u << bpp;
        if (colours > (1u << bpp))
            return BMP_UNSUPPORTED;
        if ((size_t)14 + header + (size_t)colours * 4 > n)
            return BMP_TRUNCATED;
    }
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if (offset > n || stride * (size_t)h > n - offset)
        return BMP_TRUNCATED;
    lcd_clear(lcd);
    int cols = w < LCD_W ? w : LCD_W, rows = h < LCD_H ? h : LCD_H;
    for (int y = 0; y < rows; y++) {
        const uint8_t *row = d + offset + stride * (size_t)(top_down ? y : h - 1 - y);
        for (int x = 0; x < cols; x++) {
            int r, g, b;
            if (bpp == 24 || bpp == 32) {
                const uint8_t *p = row + x * (bpp / 8);
                b = p[0]; g = p[1]; r = p[2];
            } else {
                unsigned index = bpp == 8 ? row[x] : bpp == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15
                                                              : (row[x / 8] >> (7 - (x & 7))) & 1;
                if (index >= colours)
                    return BMP_TRUNCATED;
                b = palette[index * 4]; g = palette[index * 4 + 1]; r = palette[index * 4 + 2];
            }
            if ((299 * r + 587 * g + 114 * b) / 1000 < 128)
                lcd_put(lcd, x, y, 1);
        }
    }
    if (adjusted)
        *adjusted = w != LCD_W || h != LCD_H;
    return BMP_OK;
}

/* ---- VMS header ---------------------------------------------------------------- */
uint16_t vms_crc16(const uint8_t *data, size_t bytes)
{
    unsigned crc = 0;
    while (bytes--) {
        crc ^= (unsigned)*data++ << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
        crc &= 0xffff;
    }
    return crc;
}
static uint16_t file_crc(const uint8_t *file, size_t total)
{
    unsigned crc = 0;
    for (size_t i = 0; i < total; i++) {
        unsigned byte = (i == 70 || i == 71) ? 0 : file[i];
        crc ^= byte << 8;
        for (int bit = 0; bit < 8; bit++)
            crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
        crc &= 0xffff;
    }
    return crc;
}
static void text_field(char *out, const uint8_t *in, size_t n)
{
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned ch = in[i];
        out[i] = ch >= 32 && ch < 127 ? ch : ch ? '?' : ' ';
    }
    len = n;
    while (len && out[len - 1] == ' ')
        len--;
    out[len] = 0;
}
int vms_parse(const uint8_t *f, size_t bytes, struct vms_info *info)
{
    static const unsigned eyecatch_bytes[4] = {0, 72 * 56 * 2, 512 + 72 * 56, 32 + 72 * 56 / 2};
    if (bytes < VMS_HEADER)
        return VMS_SHORT;
    unsigned icons = get16(f + 64), eyecatch = get16(f + 68);
    if (icons < 1 || icons > VMS_MAX_FRAMES)
        return VMS_ICONS;
    if (eyecatch > 3)
        return VMS_EYECATCH;
    uint32_t data_len = get32(f + 72);
    size_t head = VMS_HEADER + (size_t)icons * VMS_ICON_BYTES + eyecatch_bytes[eyecatch];
    if (data_len > bytes || head + data_len > bytes)
        return VMS_LENGTH;
    size_t total = head + data_len;
    if (file_crc(f, total) != get16(f + 70))
        return VMS_CRC;
    memset(info, 0, sizeof(*info));
    info->icons = icons;
    info->speed = get16(f + 66);
    info->eyecatch = eyecatch;
    info->data_len = data_len;
    info->total = total;
    text_field(info->short_text, f, 16);
    text_field(info->long_text, f + 16, 32);
    text_field(info->app_id, f + 48, 16);
    return VMS_OK;
}
const char *vms_error_text(int error)
{
    switch (error) {
    case VMS_SHORT: return "Not a VMS save: file is shorter than its header.";
    case VMS_ICONS: return "No icon: this is not a standard VMS save.";
    case VMS_EYECATCH: return "Unknown eyecatch type: not a standard VMS save.";
    case VMS_LENGTH: return "Header length does not fit the file.";
    case VMS_CRC: return "Header CRC does not match; the file was not edited.";
    default: return "";
    }
}
static const uint8_t *frame_at(const uint8_t *file, int frame)
{
    return file + VMS_HEADER + (size_t)frame * VMS_ICON_BYTES;
}
int vms_pixel(const uint8_t *file, int frame, int x, int y)
{
    uint8_t byte = frame_at(file, frame)[y * 16 + x / 2];
    return x & 1 ? byte & 15 : byte >> 4;
}
void vms_set_pixel(uint8_t *file, int frame, int x, int y, int index)
{
    uint8_t *p = (uint8_t *)frame_at(file, frame) + y * 16 + x / 2;
    *p = x & 1 ? (*p & 0xf0) | (index & 15) : (*p & 0x0f) | (index << 4);
}
uint16_t vms_palette(const uint8_t *file, int index)
{
    return get16(file + 96 + 2 * index);
}
void vms_set_palette(uint8_t *file, int index, uint16_t value)
{
    put16(file + 96 + 2 * index, value);
}
void vms_flip_h(uint8_t *file, int frame)
{
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 16; x++) {
            int a = vms_pixel(file, frame, x, y), b = vms_pixel(file, frame, 31 - x, y);
            vms_set_pixel(file, frame, x, y, b);
            vms_set_pixel(file, frame, 31 - x, y, a);
        }
}
void vms_flip_v(uint8_t *file, int frame)
{
    uint8_t *base = (uint8_t *)frame_at(file, frame), row[16];
    for (int y = 0; y < 16; y++) {
        memcpy(row, base + y * 16, 16);
        memcpy(base + y * 16, base + (31 - y) * 16, 16);
        memcpy(base + (31 - y) * 16, row, 16);
    }
}
void vms_fill(uint8_t *file, int frame, int index)
{
    memset((uint8_t *)frame_at(file, frame), (index & 15) * 17, VMS_ICON_BYTES);
}
void vms_seal(uint8_t *file, const struct vms_info *info)
{
    put16(file + 70, file_crc(file, info->total));
}
int argb4444_rgb(uint16_t v, int *r, int *g, int *b)
{
    int a = (v >> 12) & 15, red = (v >> 8) & 15, green = (v >> 4) & 15, blue = v & 15;
    *r = (a * red + (15 - a) * 15) * 17 / 15;
    *g = (a * green + (15 - a) * 15) * 17 / 15;
    *b = (a * blue + (15 - a) * 15) * 17 / 15;
    return a < 2;
}

/* ---- names --------------------------------------------------------------------- */
int vmu_name_ok(const char *name)
{
    size_t n = strlen(name);
    while (n && name[n - 1] == ' ')
        n--;
    if (!n || n > 12 || name[0] == ' ')
        return 0;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)name[i] < 32 || (unsigned char)name[i] > 126)
            return 0;
    return 1;
}
static char dos_char(char c)
{
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ? c : '_';
}
void dos_name_for_vmu(const char *vmu, char out[13])
{
    const char *dot = strrchr(vmu, '.');
    size_t base_len = dot ? (size_t)(dot - vmu) : strlen(vmu), n = 0;
    for (size_t i = 0; i < base_len && n < 8; i++)
        out[n++] = dos_char(vmu[i]);
    if (!n)
        out[n++] = 'V';
    out[n++] = '.';
    size_t ext = 0;
    if (dot)
        for (const char *p = dot + 1; *p && ext < 3; p++, ext++)
            out[n++] = dos_char(*p);
    if (!ext) {
        memcpy(out + n, "VMS", 3);
        n += 3;
    }
    out[n] = 0;
}
void vmu_name_for_dos(const char *dos, char out[13])
{
    size_t n = strlen(dos);
    const char *dot = strrchr(dos, '.');
    if (dot && !strcmp(dot, ".VMS"))
        n = (size_t)(dot - dos);
    if (n > 12)
        n = 12;
    memcpy(out, dos, n);
    out[n] = 0;
}
