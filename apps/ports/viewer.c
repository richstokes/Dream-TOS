/* Native GEM viewer using stb_image. Frontend GPL-2.0-or-later.
 * PNG, JPEG and BMP, bounded to 640x480 and 1 MiB encoded input. */
#include "app.h"
#include "drives.h"
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 640
#include "stb_image.h"
static unsigned char *rgb, *indices;
static int width, height, gray, mirror;
static int palette[16][3];
static const unsigned char physical[16] = {0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13};
static int distance(int r, int g, int b, int c)
{
    int dr = r - palette[c][0], dg = g - palette[c][1], db = b - palette[c][2];
    return dr * dr + dg * dg + db * db;
}
static int nearest(int r, int g, int b, int n)
{
    int best = 0, err = 200000;
    for (int i = 0; i < n; i++) {
        int d = distance(r, g, b, i);
        if (d < err) {
            err = d;
            best = i;
        }
    }
    return best;
}
static int quantize(void)
{
    unsigned int hist[4096] = {0};
    unsigned char lut[4096];
    free(indices);
    indices = malloc(width * height);
    if (!indices)
        return 0;
    for (int i = 0; i < width * height; i++) {
        int r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
        if (gray)
            r = g = b = (r * 77 + g * 150 + b * 29) >> 8;
        hist[(r >> 4) * 256 + (g >> 4) * 16 + (b >> 4)]++;
    }
    for (int k = 0; k < 3; k++) {
        palette[0][k] = 255;
        palette[1][k] = 0;
    }
    /* Weighted farthest-colour seeds followed by Lloyd refinement. Preserve
     * black/white for readable GEM controls. All arithmetic is bounded. */
    for (int n = 2; n < 16; n++) {
        uint64_t best = 0;
        int choice = 0;
        for (int i = 0; i < 4096; i++)
            if (hist[i]) {
                int r = (i >> 8) * 17, g = ((i >> 4) & 15) * 17, b = (i & 15) * 17;
                int c = nearest(r, g, b, n);
                uint64_t score = (uint64_t)distance(r, g, b, c) * hist[i];
                if (score > best) {
                    best = score;
                    choice = i;
                }
            }
        palette[n][0] = (choice >> 8) * 17;
        palette[n][1] = ((choice >> 4) & 15) * 17;
        palette[n][2] = (choice & 15) * 17;
    }
    for (int pass = 0; pass < 5; pass++) {
        unsigned int sums[16][4] = {{0}};
        for (int i = 0; i < 4096; i++)
            if (hist[i]) {
                int r = (i >> 8) * 17, g = ((i >> 4) & 15) * 17, b = (i & 15) * 17,
                    c = nearest(r, g, b, 16);
                sums[c][0] += r * hist[i];
                sums[c][1] += g * hist[i];
                sums[c][2] += b * hist[i];
                sums[c][3] += hist[i];
            }
        for (int c = 2; c < 16; c++)
            if (sums[c][3])
                for (int k = 0; k < 3; k++)
                    palette[c][k] = sums[c][k] / sums[c][3];
    }
    for (int i = 0; i < 4096; i++)
        lut[i] = nearest((i >> 8) * 17, ((i >> 4) & 15) * 17, (i & 15) * 17, 16);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            int i = y * width + (mirror ? width - 1 - x : x), r = rgb[i * 3], g = rgb[i * 3 + 1],
                b = rgb[i * 3 + 2];
            if (gray)
                r = g = b = (r * 77 + g * 150 + b * 29) >> 8;
            indices[y * width + x] = lut[(r >> 4) * 256 + (g >> 4) * 16 + (b >> 4)];
        }
    return 1;
}
static void redraw(const char *path)
{
    app_clear(0);
    char s[96];
    snprintf(s, sizeof(s), "%.60s  %d x %d", path, width, height);
    app_text(8, 44, s, 1);
    for (int c = 0; c < 16; c++)
        app_palette(c, palette[c][0] * 1000 / 255, palette[c][1] * 1000 / 255,
                    palette[c][2] * 1000 / 255);
    int w = width, h = height;
    if (h > 384) {
        w = w * 384 / h;
        h = 384;
    }
    int stride = ((w + 15) / 16) * 4;
    uint16_t *planes = calloc(stride * h, sizeof(*planes));
    if (planes) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                int c = physical[indices[(y * height / h) * width + (x * width / w)]];
                uint16_t bit = 0x8000 >> (x & 15);
                for (int p = 0; p < 4; p++)
                    if (c & (1 << p))
                        planes[y * stride + (x / 16) * 4 + p] |= bit;
            }
        app_blit(planes, w, h, (640 - w) / 2, 56 + (384 - h) / 2);
        free(planes);
    }
    app_status("O:open N:next G:grey M:mirror S:save BMP Esc:exit");
}
static int load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        app_alert("Cannot open image");
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        app_alert("Image file must be at most 1 MiB");
        return 0;
    }
    unsigned char *data = malloc(size);
    if (!data) {
        fclose(f);
        return 0;
    }
    int ok = fread(data, 1, size, f) == (size_t)size;
    fclose(f);
    int w, h, n;
    if (!ok || !stbi_info_from_memory(data, size, &w, &h, &n) || w > 640 || h > 480) {
        free(data);
        app_alert("Use PNG, JPEG or BMP at most 640 x 480");
        return 0;
    }
    unsigned char *p = stbi_load_from_memory(data, size, &w, &h, &n, 3);
    free(data);
    if (!p) {
        app_alert("Image decoding failed or memory exhausted");
        return 0;
    }
    free(rgb);
    rgb = p;
    width = w;
    height = h;
    gray = mirror = 0;
    return quantize();
}
static void put16(unsigned char *p, unsigned n)
{
    p[0] = n;
    p[1] = n >> 8;
}
static void put32(unsigned char *p, unsigned n)
{
    put16(p, n);
    put16(p + 2, n >> 16);
}
static void save(void)
{
    char path[80] = "C:\\PICTURE.BMP";
    path[0] = dc_storage_drive();
    if (!app_prompt("Export 16-colour BMP (SD drive or C:):", path, sizeof(path)))
        return;
    if (!isalpha((unsigned char)path[0]) || path[1] != ':') {
        app_alert("Give a drive, e.g. C:\\PICTURE.BMP");
        return;
    }
    if (!dc_drive_state(path[0])) {
        app_alert("That drive is read-only or not mounted");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        ai[0] = 2;
        aa[0] = (intptr_t)"[2][Replace existing image?][Replace|Cancel]";
        aes_call(52, 1, 1, 1);
        if (ao[0] != 1)
            return;
    }
    f = fopen(path, "wb");
    if (!f) {
        app_alert("Cannot create image");
        return;
    }
    int stride = (width + 3) & ~3;
    unsigned char hdr[118] = {0}, row[640] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    put32(hdr + 2, 118 + stride * height);
    put32(hdr + 10, 118);
    put32(hdr + 14, 40);
    put32(hdr + 18, width);
    put32(hdr + 22, height);
    put16(hdr + 26, 1);
    put16(hdr + 28, 8);
    put32(hdr + 34, stride * height);
    put32(hdr + 46, 16);
    for (int i = 0; i < 16; i++) {
        hdr[54 + i * 4] = palette[i][2];
        hdr[55 + i * 4] = palette[i][1];
        hdr[56 + i * 4] = palette[i][0];
    }
    int ok = fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    for (int y = height - 1; y >= 0; y--) {
        memcpy(row, indices + y * width, width);
        if (fwrite(row, 1, stride, f) != (size_t)stride)
            ok = 0;
    }
    if (fclose(f))
        ok = 0;
    char done[64];
    snprintf(done, sizeof(done), "Image saved on %c:%s", toupper((unsigned char)path[0]), dc_drive_note(path[0]));
    app_alert(ok ? done : "Image save failed");
}
int app_main(int argc, char **argv)
{
    if (!app_begin("IMAGES - stb_image native GEM viewer"))
        return 1;
    atexit(app_end);
    char path[80];
    snprintf(path, sizeof(path), "%s", argc > 1 ? argv[1] : "D:\\SONIC.PNG");
    int sample = 0;
    if (!load(path))
        return 1;
    redraw(path);
    for (;;) {
        int key = app_key() & 255;
        if (key == 27)
            break;
        if (key == 'n' || key == 'N') {
            sample ^= 1;
            snprintf(path, sizeof(path), "D:\\%s.PNG", sample ? "SPACE" : "SONIC");
            load(path);
        }
        if (key == 'o' || key == 'O') {
            char next[80];
            strcpy(next, path);
            if (app_prompt("Open PNG / JPEG / BMP:", next, sizeof(next)) && load(next))
                strcpy(path, next);
        }
        if (key == 'g' || key == 'G') {
            gray = !gray;
            quantize();
        }
        if (key == 'm' || key == 'M') {
            mirror = !mirror;
            quantize();
        }
        if (key == 's' || key == 'S')
            save();
        redraw(path);
    }
    free(indices);
    free(rgb);
    return 0;
}
