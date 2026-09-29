/* Host test of the paint program's pure logic: shapes, flood fill, undo and BMP. */
#define app_main guest_app_main
#include "../apps/ports/paint.c"
#include <assert.h>
static int px(int x, int y)
{
    return cv[y * CW + x];
}
static long count(int c)
{
    long n = 0;
    for (int i = 0; i < CW * CH; i++)
        n += cv[i] == c;
    return n;
}
static int same(const uint8_t *a, const uint8_t *b)
{
    return !memcmp(a, b, CW * CH);
}
static uint8_t *snap(void)
{
    uint8_t *s = malloc(CW * CH);
    memcpy(s, cv, CW * CH);
    return s;
}
static void wr(const char *name, const uint8_t *d, size_t n)
{
    FILE *f = fopen(name, "wb");
    assert(f && fwrite(d, 1, n, f) == n && !fclose(f));
}
int main(void)
{
    assert(canvas_alloc());
    /* Primitives clip to the canvas and track dirty rectangles. */
    pset(-1, 5, 3);
    pset(CW, 5, 3);
    pset(5, CH, 3);
    assert(count(3) == 0 && dx1 < dx0);
    pset(10, 20, 2);
    assert(px(10, 20) == 2 && dx0 == 10 && dx1 == 10 && dy0 == 20 && dy1 == 20);
    revert_dirty();
    assert(px(10, 20) == 0 && dx1 < dx0);
    draw_line(0, 0, 99, 49, 1, 1);
    assert(px(0, 0) == 1 && px(99, 49) == 1 && count(1) == 100);
    draw_line(99, 49, 0, 0, 1, 2); /* reversed line covers the same pixels */
    assert(count(2) == 100 && count(1) == 0);
    commit();
    stamp(50, 50, 4, 5);
    assert(px(50, 50) == 5 && px(49, 49) == 5 && px(51, 50) == 5 && px(48, 48) == 0 && px(47, 50) == 0 && px(50, 47) == 0);
    revert_dirty();
    /* Rectangles and the flood fill inside them. */
    new_canvas();
    draw_rect(100, 100, 200, 150, 1, 1);
    assert(px(100, 100) == 1 && px(200, 150) == 1 && px(150, 100) == 1 && px(150, 125) == 0 &&
           count(1) == 2 * 101 + 2 * 49);
    assert(flood(150, 125, 4));
    assert(count(4) == 99 * 49 && px(100, 100) == 1 && px(99, 99) == 0);
    assert(!flood(150, 125, 4)); /* same colour is a no-op */
    assert(flood(0, 0, 6) && count(0) == 0 && count(6) == CW * CH - 101 * 51);
    fill_rect(300, 300, 250, 260, 9);
    assert(count(9) == 51 * 41 && px(250, 260) == 9 && px(300, 300) == 9);
    /* Nested boxes exercise every fill branch; the fill must not leak. */
    new_canvas();
    for (int i = 0; i < 6; i++)
        draw_rect(10 + i * 10, 10 + i * 10, 200 - i * 10, 200 - i * 10, 1, 1 + (i & 1));
    flood(105, 105, 7);
    assert(px(105, 105) == 7 && px(5, 5) == 0);
    /* Ellipses stay in their box, are symmetrical and have the right area. */
    new_canvas();
    draw_ellipse(100, 100, 300, 200, 1, 3, 1);
    assert(px(200, 150) == 3 && px(100, 100) == 0 && px(300, 200) == 0 && px(99, 150) == 0 && px(301, 150) == 0);
    long area = count(3), want = (long)(3.14159265 * 100 * 50);
    assert(labs(area - want) < want / 20);
    for (int y = 100; y <= 200; y++)
        for (int x = 100; x <= 300; x++)
            assert(px(x, y) == px(400 - x, 300 - y) || x == 100 || x == 300);
    new_canvas();
    dirty_reset();
    draw_ellipse(100, 100, 300, 200, 1, 3, 0);
    assert(px(200, 100) == 3 && px(200, 200) == 3 && px(100, 150) == 3 && px(300, 150) == 3 && px(200, 150) == 0);
    assert(dx0 == 100 && dx1 == 300 && dy0 == 100 && dy1 == 200);
    assert(flood(200, 150, 5) && px(0, 0) == 0 && count(5) > 9000);
    new_canvas();
    draw_ellipse(10, 10, 10, 30, 1, 2, 0); /* degenerate: a line */
    assert(count(2) == 21);
    /* Multi-level undo and redo restore exact pixels. */
    new_canvas();
    uint8_t *s[5];
    s[0] = snap();
    for (int i = 1; i < 5; i++) {
        draw_line(20 * i, 10, 20 * i + 50, 300, 3, i);
        assert(commit());
        s[i] = snap();
    }
    assert(un == 4 && uc == 4);
    for (int i = 3; i >= 0; i--)
        assert(undo() && same(cv, s[i]) && same(bk, s[i]));
    assert(!undo() && !commit());
    for (int i = 1; i < 5; i++)
        assert(redo() && same(cv, s[i]));
    assert(!redo());
    assert(undo() && undo());
    draw_rect(5, 5, 9, 9, 1, 8); /* a new action discards the redo branch */
    assert(commit() && un == 3 && !redo());
    assert(undo() && same(cv, s[2]));
    for (int i = 0; i < 5; i++)
        free(s[i]);
    /* Cancelled shape strokes leave no trace. */
    uint8_t *before = snap();
    stroke = 0;
    tool = T_LINE;
    scol = 2;
    ax = 0;
    ay = 0;
    shape(300, 300);
    shape(400, 20);
    assert(!same(cv, before));
    revert_dirty();
    assert(same(cv, before));
    free(before);
    /* Memory stays bounded: full-canvas actions evict the oldest levels. */
    history_clear();
    for (int i = 0; i < 200; i++) {
        canvas_fill_all(i & 15);
        assert(commit());
        assert(un <= UNDO_MAX && ubytes <= UNDO_BUDGET);
    }
    assert(un > 0 && un <= UNDO_MAX && ubytes > 0);
    int levels = un;
    while (undo())
        ;
    assert(uc == 0 && levels == un);
    history_clear();
    assert(ubytes == 0 && un == 0);
    /* BMP: roundtrip through memory, then through the save/open paths. */
    new_canvas();
    for (int i = 0; i < CW * CH; i++)
        cv[i] = (i * 7 + i / CW) & 15;
    uint8_t *buf = malloc(bmp_size(CW, CH)), *dst = malloc(CW * CH);
    size_t n = bmp_encode(cv, CW, CH, buf);
    assert(n == 118 + CW * CH && buf[0] == 'B' && rd32(buf + 18) == CW && rd32(buf + 22) == CH &&
           rd16(buf + 28) == 8);
    assert(!bmp_decode(buf, n, dst) && same(dst, cv));
    /* Odd width exercises row padding and white padding of small images. */
    uint8_t small[7 * 5];
    for (int i = 0; i < 35; i++)
        small[i] = i & 15;
    n = bmp_encode(small, 7, 5, buf);
    assert(n == 118 + 8 * 5 && !bmp_decode(buf, n, dst));
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            assert(dst[y * CW + x] == (x < 7 && y < 5 ? small[y * 7 + x] : 0));
    /* Malformed and truncated input is rejected. */
    assert(bmp_decode(buf, 10, dst) && bmp_decode(buf, n - 1, dst));
    buf[0] = 'X';
    assert(bmp_decode(buf, n, dst));
    buf[0] = 'B';
    put32(buf + 18, 641);
    assert(bmp_decode(buf, n, dst));
    put32(buf + 18, 0x80000000u);
    assert(bmp_decode(buf, n, dst));
    put32(buf + 18, 7);
    put32(buf + 10, 0xffffff00u);
    assert(bmp_decode(buf, n, dst));
    put32(buf + 10, 118);
    put32(buf + 30, 1); /* RLE */
    assert(bmp_decode(buf, n, dst));
    /* A viewer-style BMP has an arbitrary palette: colours snap to ours. */
    uint8_t v[54 + 16 * 4 + 4 * 2] = {0};
    v[0] = 'B';
    v[1] = 'M';
    put32(v + 10, 54 + 64);
    put32(v + 14, 40);
    put32(v + 18, 3);
    put32(v + 22, 2);
    put16(v + 26, 1);
    put16(v + 28, 8);
    put32(v + 46, 16);
    static const uint8_t ent[3][3] = {{250, 250, 250}, {5, 5, 5}, {240, 10, 10}}; /* r,g,b */
    for (int i = 0; i < 3; i++) {
        v[54 + i * 4] = ent[i][2];
        v[55 + i * 4] = ent[i][1];
        v[56 + i * 4] = ent[i][0];
    }
    v[54 + 64] = 0;
    v[55 + 64] = 1;
    v[56 + 64] = 2; /* bottom row */
    v[54 + 68] = 2;
    v[55 + 68] = 1;
    v[56 + 68] = 0; /* top row */
    assert(!bmp_decode(v, sizeof(v), dst));
    assert(dst[0] == 2 && dst[1] == 1 && dst[2] == 0 && dst[CW] == 0 && dst[CW + 1] == 1 && dst[CW + 2] == 2);
    /* 24-bit, top-down. */
    uint8_t t[54 + 8] = {0};
    t[0] = 'B';
    t[1] = 'M';
    put32(t + 10, 54);
    put32(t + 14, 40);
    put32(t + 18, 2);
    put32(t + 22, (uint32_t)-1);
    put16(t + 26, 1);
    put16(t + 28, 24);
    t[56] = 255; /* red (BGR order) */
    t[58] = 128; /* dark green */
    assert(!bmp_decode(t, sizeof(t), dst) && dst[0] == 2 && dst[1] == 11);
    /* Save writes a file we can open again. */
    new_canvas();
    draw_ellipse(30, 30, 400, 300, 2, 4, 1);
    commit();
    uint8_t *drawn = snap();
    assert(save_bmp(0) && !modified && fname[0]);
    char saved[80];
    snprintf(saved, sizeof(saved), "%s", fname);
    assert(save_bmp(0)); /* resave to the same name needs no prompt */
    new_canvas();
    assert(load_bmp(saved) && same(cv, drawn) && same(bk, drawn) && !modified && un == 0);
    assert(!load_bmp("no-such-file.bmp"));
    wr("BAD.BMP", (const uint8_t *)"BMnope", 6);
    assert(!load_bmp("BAD.BMP") && same(cv, drawn));
    remove("BAD.BMP");
    remove(saved);
    free(drawn);
    free(buf);
    free(dst);
    history_clear();
    free(cv);
    free(bk);
    return 0;
}
