#define app_main guest_app_main
#include "../apps/ports/viewer.c"
#include <assert.h>
int main(int argc, char **argv)
{
    assert(argc == 3);
    assert(load(argv[1]));
    assert(width == 320 && height == 240);
    for (int i = 0; i < width * height; i++)
        assert(indices[i] < 16);
    assert(load(argv[2]));
    gray = 1;
    mirror = 1;
    assert(quantize());
    for (int c = 0; c < 16; c++)
        assert(palette[c][0] == palette[c][1] && palette[c][1] == palette[c][2]);
    save();
    FILE *f = fopen("C:\\PICTURE.BMP", "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    int n = ftell(f);
    rewind(f);
    unsigned char *b = malloc(n);
    assert(fread(b, 1, n, f) == (size_t)n);
    fclose(f);
    int w, h, ch;
    unsigned char *p = stbi_load_from_memory(b, n, &w, &h, &ch, 3);
    assert(p && w == width && h == height);
    for (int i = 0; i < w * h; i++)
        for (int c = 0; c < 3; c++)
            assert(p[3 * i + c] == palette[indices[i]][c]);
    free(b);
    free(p);
    free(rgb);
    free(indices);
    remove("C:\\PICTURE.BMP");
    return 0;
}
