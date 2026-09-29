/* VMU Editor end to end on the host: the real GUI logic (apps/ports/vmuedit.c)
 * drives the real file-service engine and KOS-vmufs double over an in-memory card.
 * Display calls are stubs; mouse and keyboard input are synthesized through the
 * same step() function the program's event loop uses. Host files are created
 * in the current directory with names like "C:\NOTE.TXT" (a backslash is an
 * ordinary character on the host), and a fake GEMDOS Fsfirst lists them. */
#define VMUEDIT_TEST
static unsigned long fake_time = 1000;
#define app_millis test_millis
#include "../apps/ports/vmuedit.c"
#include <assert.h>
#include <dirent.h>
#include <unistd.h>
#include <dc/maple.h>
#include <dc/maple/vmu.h>
int vmufs_write(maple_device_t *dev, const char *fn, void *inbuf, int insize, int flags);
int vmufs_delete(maple_device_t *dev, const char *fn);
unsigned long test_millis(void) { return fake_time; }

/* ---- in-memory card ---- */
static unsigned char image[256][512];
static maple_device_t device = {{MAPLE_FUNC_MEMCARD | MAPLE_FUNC_LCD}, 0, 0};
static int block_writes, allow_writes = 1;
int vmu_block_read(maple_device_t *d, uint16_t block, uint8_t *out) { (void)d; if (block > 255) return -1; memcpy(out, image[block], 512); return 0; }
int vmu_block_write(maple_device_t *d, uint16_t block, const uint8_t *in)
{
    (void)d;
    block_writes++;
    assert(block != 255); /* the root block is never written */
    if (block > 255 || !allow_writes) return -1;
    memcpy(image[block], in, 512);
    return 0;
}
static void put16(unsigned char *p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static void format_card(void) /* test fixture only: no OS path can do this */
{
    memset(image, 0, sizeof(image));
    memset(image[255], 0x55, 16);
    put16(image[255] + 70, 254); put16(image[255] + 72, 1); put16(image[255] + 74, 253);
    put16(image[255] + 76, 13); put16(image[255] + 80, 200);
    for (int i = 0; i < 256; i++) put16(image[254] + 2 * i, i < 200 ? 0xfffc : 0xfffa);
}
static int backend_read(void *c, unsigned b, unsigned char *out) { return vmu_block_read(c, b, out); }
static int backend_put(void *c, const char *n, const unsigned char *d, unsigned blocks, int ow) { (void)c; return vmufs_write(&device, n, (void *)d, blocks * 512, ow ? 1 : 0); }
static int backend_erase(void *c, const char *n) { (void)c; return vmufs_delete(&device, n); }
static unsigned char stage[DC_VMUF_MAX_BYTES], backup[DC_VMUF_MAX_BYTES];
static struct dc_vmu_backend backend = {backend_read, backend_put, backend_erase, stage, backup};

static int screen_pushes, screen_fail;
static uint8_t screen_last[LCD_BYTES];
static long fake_screen(uint32_t port, uint32_t unit, const void *bitmap, uint32_t bytes)
{
    assert(port == 0 && unit == 0 && bytes == LCD_BYTES);
    if (screen_fail) return screen_fail;
    memcpy(screen_last, bitmap, bytes);
    screen_pushes++;
    return 0;
}
static long fake_info(uint32_t port, uint32_t unit, void *buffer, uint32_t bytes)
{
    assert(port == 0 && unit == 0 && bytes == sizeof(struct dc_vmu_info));
    struct dc_vmu_info *out = buffer;
    memset(out, 0, bytes);
    out->version = DC_VMU_VERSION; out->bytes = bytes;
    out->status = dc_vmu_inspect(out, backend_read, &device);
    if (out->status) out->file_count = out->free_blocks = out->total_blocks = 0;
    return bytes;
}
static long fake_file_read(uint32_t p, uint32_t u, const char *n, void *b, uint32_t sz) { assert(!p && !u); return dc_vmuf_read(&backend, &device, n, b, sz); }
static long fake_file_write(uint32_t p, uint32_t u, const char *n, const void *d, uint32_t sz, uint32_t f) { assert(!p && !u); return dc_vmuf_write(&backend, &device, n, d, sz, f); }
static long fake_file_delete(uint32_t p, uint32_t u, const char *n) { assert(!p && !u); return dc_vmuf_delete(&backend, &device, n); }
static long fake_system_info(void *buffer, uint32_t bytes)
{
    struct dc_system_info *info = buffer;
    assert(bytes == sizeof(*info));
    memset(info, 0, bytes);
    info->version = DC_SYSTEM_INFO_VERSION; info->bytes = bytes;
    info->drive_mask = 0x1c;    /* C: D: E: */
    info->readonly_mask = 0x08; /* D: */
    info->volatile_mask = 0x04; /* C: */
    info->device_count = 2;
    info->devices[0] = (struct dc_device_info){0, 0, 0x02000000 | 0x04000000, "Visual Memory"};
    info->devices[1] = (struct dc_device_info){0, 5, 0x01000000, "Not a card"};
    return bytes;
}

/* Fake GEMDOS Fsfirst/Fsnext over host files named "X:\..." in the current directory. */
static char dta_area[44];
static void *current_dta = dta_area;
static DIR *scan_dir;
static char scan_prefix[100];
static int next_match(void)
{
    struct dirent *e;
    while ((e = readdir(scan_dir))) {
        size_t n = strlen(scan_prefix);
        if (strncmp(e->d_name, scan_prefix, n)) continue;
        const char *rest = e->d_name + n;
        const char *slash = strchr(rest, '\\');
        char *dta = current_dta;
        memset(dta, 0, 44);
        if (slash) { /* a file inside a sub-folder: report the folder once (test keeps names unique) */
            dta[21] = 0x10;
            snprintf(dta + 30, 14, "%.*s", (int)(slash - rest), rest);
        } else {
            uint32_t size = 0;
            FILE *f = fopen(e->d_name, "rb");
            if (f) { fseek(f, 0, SEEK_END); size = ftell(f); fclose(f); }
            memcpy(dta + 26, &size, 4);
            snprintf(dta + 30, 14, "%s", rest);
        }
        return 0;
    }
    closedir(scan_dir);
    scan_dir = NULL;
    return -33;
}
static long fake_gemdos(int op, ...)
{
    va_list ap;
    va_start(ap, op);
    long result = 0;
    if (op == 0x2f) result = (long)(intptr_t)current_dta;
    else if (op == 0x1a) current_dta = va_arg(ap, void *);
    else if (op == 0x4e) {
        const char *pattern = va_arg(ap, const char *);
        size_t n = strlen(pattern);
        assert(n > 3 && !strcmp(pattern + n - 3, "*.*"));
        snprintf(scan_prefix, sizeof(scan_prefix), "%.*s", (int)(n - 3), pattern);
        if (scan_dir) closedir(scan_dir);
        scan_dir = opendir(".");
        result = next_match();
    } else if (op == 0x4f) result = scan_dir ? next_match() : -33;
    else assert(!"unexpected GEMDOS call");
    va_end(ap);
    return result;
}
static struct dc_native_api api = {.size = sizeof(api), .gemdos = fake_gemdos, .system_info = fake_system_info,
    .vmu_info = fake_info, .vmu_file_read = fake_file_read, .vmu_file_write = fake_file_write,
    .vmu_file_delete = fake_file_delete, .vmu_screen = fake_screen};
extern const struct dc_native_api *dc_os;

/* ---- scripted dialogs ---- */
static char scripted_path[100], scripted_name[64], last_confirm[300];
static int confirm_answer, confirm_calls, alert_calls, choose_ok = 1, prompt_ok = 1;
static int t_confirm(const char *text, const char *yes) { snprintf(last_confirm, sizeof(last_confirm), "%s [%s]", text, yes); confirm_calls++; return confirm_answer; }
static int t_choose(const char *title, int dir, char *out, size_t cap) { (void)title; (void)dir; if (!choose_ok) return 0; snprintf(out, cap, "%s", scripted_path); return 1; }
static int t_prompt(const char *label, char *buf, size_t cap) { (void)label; if (!prompt_ok) return 0; if (scripted_name[0]) snprintf(buf, cap, "%s", scripted_name); return 1; }
static void t_alert(const char *text) { (void)text; alert_calls++; }

/* ---- helpers ---- */
static void host_write(const char *name, const void *data, size_t n) { FILE *f = fopen(name, "wb"); assert(f && fwrite(data, 1, n, f) == n); fclose(f); }
static size_t host_read(const char *name, void *data, size_t max) { FILE *f = fopen(name, "rb"); if (!f) return (size_t)-1; size_t n = fread(data, 1, max, f); fclose(f); return n; }
static void redraw(void) { hot_count = 0; need_draw = 0; draw_all(); }
static void tap(int x, int y, int buttons) { step(0, x, y, buttons); step(0, x, y, 0); fake_time += 100; step(0, 0, 0, 0); }
static void click_id(int id)
{
    redraw();
    for (int i = 0; i < hot_count; i++)
        if (hot[i].id == id) { tap(hot[i].x + hot[i].w / 2, hot[i].y + hot[i].h / 2, 1); return; }
    assert(!"button not on screen (disabled?)");
}
static int button_enabled(int id) { redraw(); for (int i = 0; i < hot_count; i++) if (hot[i].id == id) return 1; return 0; }
static void key(int k) { step(k, 0, 0, 0); }
static void reload(void) { scan_cards(); read_card(); }
static void fill(unsigned char *p, size_t n, int seed) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(i * 31 + seed); }
static unsigned char snapshot[256][512];
static void add_card_file(const char *name, const void *data, size_t n, int protect, int type)
{
    assert(dc_vmuf_write(&backend, &device, name, data, n, 0) > 0);
    if (protect || type) {
        struct dc_vmu_info info;
        struct dc_vmu_layout layout;
        static unsigned char raw[DC_VMU_FILES][12];
        assert(dc_vmu_inspect_ex(&info, raw, &layout, backend_read, &device) == 0);
        for (unsigned i = 0; i < info.file_count; i++)
            if (!strcmp(info.files[i].name, name)) {
                unsigned char *entry = image[layout.dir_loc - i / 16] + 32 * (i % 16);
                if (protect) entry[1] = 0xff;
                if (type) entry[0] = type;
            }
    }
}

int main(void)
{
    dc_os = &api;
    ui_confirm = t_confirm; ui_choose = t_choose; ui_prompt = t_prompt; ui_alert = t_alert;
    save_palette();
    char tmpdir[] = "/tmp/vmueditXXXXXX";
    assert(mkdtemp(tmpdir) && chdir(tmpdir) == 0);
    format_card();
    reload();
    assert(service_ok && write_ok && lcd_ok && card_count == 1 && card.status == DC_VMU_OK && card.free_blocks == 200);
    unsigned char a[4096], b[4096];

    /* ---- Import a new file (with confirmation) ---- */
    fill(a, 1300, 1);
    host_write("C:\\NOTE.TXT", a, 1300);
    snprintf(scripted_path, sizeof(scripted_path), "C:\\NOTE.TXT");
    scripted_name[0] = 0;
    confirm_answer = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_IMPORT);
    assert(confirm_calls == 1 && strstr(last_confirm, "Copy to A0 as NOTE.TXT") && !memcmp(snapshot, image, sizeof(image)));
    confirm_answer = 1;
    click_id(C_IMPORT);
    assert(card.file_count == 1 && !strcmp(card.files[0].name, "NOTE.TXT") && card.files[0].blocks == 3 && card.free_blocks == 197);
    assert(message_colour == BLACK && strstr(message, "verified"));
    assert(fake_file_read(0, 0, "NOTE.TXT", stage, 2048) == 1536 && !memcmp(stage, a, 1300) && !stage[1300]);
    assert(selected == 0);

    /* ---- Overwrite needs its own, stronger confirmation ---- */
    fill(b, 700, 9);
    host_write("C:\\NOTE.TXT", b, 700);
    confirm_answer = 0; confirm_calls = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_IMPORT);
    assert(confirm_calls == 1 && strstr(last_confirm, "Replace NOTE.TXT") && !memcmp(snapshot, image, sizeof(image)));
    confirm_answer = 1;
    click_id(C_IMPORT);
    assert(card.files[0].blocks == 2 && card.free_blocks == 198);
    assert(fake_file_read(0, 0, "NOTE.TXT", stage, 2048) == 1024 && !memcmp(stage, b, 700));

    /* ---- Rejections happen before any write ---- */
    memcpy(snapshot, image, sizeof(image));
    scripted_name[0] = 0;
    { static unsigned char big[DC_VMUF_MAX_BYTES + 10]; host_write("C:\\BIG.BIN", big, sizeof(big)); }
    snprintf(scripted_path, sizeof(scripted_path), "C:\\BIG.BIN");
    confirm_calls = 0;
    click_id(C_IMPORT);
    assert(!confirm_calls && message_colour == RED && strstr(message, "over the VMU limit") && !memcmp(snapshot, image, sizeof(image)));
    host_write("C:\\EMPTY.DAT", "", 0);
    snprintf(scripted_path, sizeof(scripted_path), "C:\\EMPTY.DAT");
    click_id(C_IMPORT);
    assert(!confirm_calls && strstr(message, "empty"));
    snprintf(scripted_path, sizeof(scripted_path), "C:\\MISSING.DAT");
    click_id(C_IMPORT);
    assert(!confirm_calls && strstr(message, "Cannot open"));
    snprintf(scripted_path, sizeof(scripted_path), "C:\\NOTE.TXT");
    snprintf(scripted_name, sizeof(scripted_name), "TAB\tNAME");
    click_id(C_IMPORT);
    assert(!confirm_calls && strstr(message, "1 to 12"));
    snprintf(scripted_name, sizeof(scripted_name), " LEAD");
    click_id(C_IMPORT);
    assert(!confirm_calls && strstr(message, "1 to 12"));
    scripted_name[0] = 0;
    prompt_ok = 0;
    click_id(C_IMPORT);
    assert(!confirm_calls);
    prompt_ok = 1;
    assert(!memcmp(snapshot, image, sizeof(image)));

    /* ---- Export as a raw file with a sensible name ---- */
    snprintf(scripted_path, sizeof(scripted_path), "C:\\");
    snprintf(scripted_name, sizeof(scripted_name), "OUT.BIN");
    confirm_calls = 0;
    confirm_answer = 0;
    click_id(C_EXPORT);
    assert(confirm_calls == 1 && strstr(last_confirm, "Copy NOTE.TXT") && host_read("C:\\OUT.BIN", b, 4096) == (size_t)-1);
    confirm_answer = 1;
    click_id(C_EXPORT);
    assert(host_read("C:\\OUT.BIN", b, 4096) == 1024 && !memcmp(b, a + 0, 0) && strstr(message, "Saved"));
    fill(a, 700, 9);
    assert(!memcmp(b, a, 700));
    /* read-only drive and overwrite prompt */
    snprintf(scripted_path, sizeof(scripted_path), "D:\\");
    click_id(C_EXPORT);
    assert(strstr(message, "read-only") && host_read("D:\\OUT.BIN", b, 10) == (size_t)-1);
    snprintf(scripted_path, sizeof(scripted_path), "C:\\");
    confirm_calls = 0;
    click_id(C_EXPORT);
    assert(confirm_calls == 1 && strstr(last_confirm, "already exists"));
    snprintf(scripted_name, sizeof(scripted_name), "BAD*.BIN");
    click_id(C_EXPORT);
    assert(strstr(message, "plain 8.3"));

    /* ---- Files with unusual names get sensible DOS names ---- */
    add_card_file("SONIC_SYS_01", b, 100, 0, 0);
    reload();
    assert(card.file_count == 2);
    select_named("SONIC_SYS_01");
    scripted_name[0] = 0;
    confirm_answer = 1;
    unlink("C:\\SONIC_SY.VMS");
    click_id(C_EXPORT);
    assert(host_read("C:\\SONIC_SY.VMS", b, 4096) == 512 && strstr(message, "Saved"));

    /* ---- Rename: copy, verify, then delete; never overwrites ---- */
    select_named("NOTE.TXT");
    snprintf(scripted_name, sizeof(scripted_name), "SONIC_SYS_01");
    click_id(C_RENAME);
    assert(strstr(message, "never overwrites") && find_on_card("NOTE.TXT") >= 0);
    snprintf(scripted_name, sizeof(scripted_name), "NEWNAME");
    confirm_answer = 0; confirm_calls = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_RENAME);
    assert(confirm_calls == 1 && strstr(last_confirm, "Rename NOTE.TXT to NEWNAME") && !memcmp(snapshot, image, sizeof(image)));
    confirm_answer = 1;
    click_id(C_RENAME);
    assert(find_on_card("NOTE.TXT") < 0 && find_on_card("NEWNAME") >= 0 && card.file_count == 2 && card.free_blocks == 197);
    assert(fake_file_read(0, 0, "NEWNAME", stage, 2048) == 1024 && !memcmp(stage, a, 700));
    assert(selected == find_on_card("NEWNAME"));

    /* ---- Delete needs confirmation ---- */
    confirm_answer = 0; confirm_calls = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_DELETE);
    assert(confirm_calls == 1 && strstr(last_confirm, "cannot be undone") && !memcmp(snapshot, image, sizeof(image)));
    key('d');
    assert(confirm_calls == 2 && !memcmp(snapshot, image, sizeof(image)));
    confirm_answer = 1;
    key(KEY_DELETE);
    assert(card.file_count == 1 && find_on_card("NEWNAME") < 0 && card.free_blocks == 199);

    /* ---- Protected files and games: listed, exportable if not copy-protected, never modified ---- */
    add_card_file("LOCKED", a, 600, 1, 0);
    add_card_file("GAME", a, 600, 0, 0xcc);
    reload();
    memcpy(snapshot, image, sizeof(image));
    select_named("LOCKED");
    assert(!button_enabled(C_DELETE) && !button_enabled(C_RENAME) && !button_enabled(C_ICON) && !button_enabled(C_EXPORT));
    key('d'); key('n'); key('c');
    assert(!memcmp(snapshot, image, sizeof(image)));
    select_named("GAME");
    assert(!button_enabled(C_DELETE) && !button_enabled(C_RENAME) && button_enabled(C_EXPORT));
    key('d'); key('n');
    assert(!memcmp(snapshot, image, sizeof(image)));
    /* Importing over a protected name is refused by the app and (if forced) by the OS. */
    snprintf(scripted_path, sizeof(scripted_path), "C:\\OUT.BIN");
    snprintf(scripted_name, sizeof(scripted_name), "LOCKED");
    confirm_calls = 0;
    click_id(C_IMPORT);
    assert(!confirm_calls && strstr(message, "left alone") && !memcmp(snapshot, image, sizeof(image)));
    assert(fake_file_write(0, 0, "LOCKED", a, 10, DC_VMUF_OVERWRITE) == DC_VMUF_PROTECTED);
    assert(fake_file_delete(0, 0, "GAME") == DC_VMUF_PROTECTED && !memcmp(snapshot, image, sizeof(image)));

    /* ---- A damaged card is never written to ---- */
    memcpy(snapshot, image, sizeof(image));
    put16(image[253] + 2, 0); /* first file entry now shares its first block with another */
    memcpy(image[253] + 32 + 2, image[253] + 2, 2);
    unsigned char damaged[256][512];
    memcpy(damaged, image, sizeof(image));
    reload();
    assert(card.status == DC_VMU_CORRUPT && !button_enabled(C_IMPORT) && !button_enabled(C_DELETE));
    key('i'); key('d');
    assert(!memcmp(damaged, image, sizeof(image)) && strstr(card_status_text(card.status), "damaged"));
    memcpy(image, snapshot, sizeof(image));
    reload();
    assert(card.status == DC_VMU_OK);

    /* ---- Card write failure is reported, and the OS keeps the old file ---- */
    fill(a, 1000, 5);
    host_write("C:\\FAIL.BIN", a, 1000);
    snprintf(scripted_path, sizeof(scripted_path), "C:\\FAIL.BIN");
    snprintf(scripted_name, sizeof(scripted_name), "FAILING");
    confirm_answer = 1;
    allow_writes = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_IMPORT);
    allow_writes = 1;
    assert(message_colour == RED && strstr(message, "Card error") && !memcmp(snapshot, image, sizeof(image)));

    /* ---- Chooser lists files through Fsfirst/Fsnext ---- */
    {
        host_write("C:\\SUB\\INNER.TXT", "x", 1);
        strcpy(choose_dir, "C:\\");
        list_directory(choose_dir);
        int folders = 0, files = 0, found_inner = 0;
        for (int i = 0; i < entry_count; i++) { folders += entries[i].dir; files += !entries[i].dir; found_inner |= !strcmp(entries[i].name, "SUB") && entries[i].dir; }
        assert(found_inner && folders >= 1 && files >= 3);
        assert(entries[0].dir); /* folders sort first */
        strcpy(choose_dir, "C:\\SUB");
        list_directory(choose_dir);
        assert(entry_count == 1 && !strcmp(entries[0].name, "INNER.TXT") && entries[0].size == 1);
        assert(at_root("C:\\") && !at_root("C:\\SUB"));
        char path[80];
        join(path, sizeof(path), "C:\\", "A.B"); assert(!strcmp(path, "C:\\A.B"));
        join(path, sizeof(path), "C:\\SUB", "A.B"); assert(!strcmp(path, "C:\\SUB\\A.B"));
        char dir[80] = "C:\\SUB\\DEEP"; parent_dir(dir); assert(!strcmp(dir, "C:\\SUB")); parent_dir(dir); assert(!strcmp(dir, "C:\\")); parent_dir(dir); assert(!strcmp(dir, "C:\\"));
        next_drive(dir); assert(!strcmp(dir, "D:\\")); next_drive(dir); next_drive(dir); assert(!strcmp(dir, "C:\\"));
    }

    /* ---- LCD editor: mouse, keyboard, operations, live push, files ---- */
    command(C_LCD);
    assert(view == VIEW_LCD);
    redraw();
    memset(lcd, 0, sizeof(lcd));
    lcd_dirty = 0; screen_pushes = 0;
    int cell_x = GX + 3 * CELL + 2, cell_y = GY + 2 * CELL + 2;
    tap(cell_x, cell_y, 1);
    assert(lcd_get(lcd, 3, 2) && lcd_dirty && screen_pushes >= 1 && !memcmp(screen_last, lcd, LCD_BYTES));
    /* Drag draws a gap-free line; right button erases. */
    step(0, GX + 0 * CELL + 1, GY + 10 * CELL + 1, 1);
    step(0, GX + 20 * CELL + 1, GY + 12 * CELL + 1, 1);
    step(0, GX + 20 * CELL + 1, GY + 12 * CELL + 1, 0);
    int run = 0;
    for (int x = 0; x <= 20; x++) { int on = 0; for (int y = 10; y <= 12; y++) on |= lcd_get(lcd, x, y); run += on; }
    assert(run == 21 && lcd_get(lcd, 0, 10) && lcd_get(lcd, 20, 12));
    step(0, GX + 5 * CELL + 1, GY + 10 * CELL + 1, 2); step(0, GX + 5 * CELL + 1, GY + 10 * CELL + 1, 0);
    assert(!lcd_get(lcd, 5, 10) || !lcd_get(lcd, 5, 11));
    /* Painting outside the grid does nothing; buttons still work. */
    uint8_t before[LCD_BYTES];
    memcpy(before, lcd, sizeof(before));
    tap(GX + 50 * CELL, GY + 5, 1);
    assert(!memcmp(before, lcd, sizeof(before)));
    click_id(C_L_INVERT);
    assert(lcd_get(lcd, 4, 4) && !lcd_get(lcd, 3, 2));
    assert(screen_pushes >= 2 && !memcmp(screen_last, lcd, LCD_BYTES));
    click_id(C_L_UNDO);
    assert(!memcmp(before, lcd, sizeof(before)));
    click_id(C_L_UNDO); /* undo toggles: redo */
    assert(lcd_get(lcd, 4, 4));
    click_id(C_L_CLEAR);
    assert(!memcmp(lcd, (uint8_t[LCD_BYTES]){0}, LCD_BYTES));
    click_id(C_L_UNDO);
    click_id(C_L_FLIPH); click_id(C_L_FLIPH); click_id(C_L_FLIPV); click_id(C_L_FLIPV);
    click_id(C_L_LEFT); click_id(C_L_RIGHT); click_id(C_L_UP); click_id(C_L_DOWN);
    assert(lcd_get(lcd, 4, 4));
    /* Keyboard editing and pen mode. */
    memset(lcd, 0, sizeof(lcd));
    lcd_cx = lcd_cy = 0;
    key(' ');
    assert(lcd_get(lcd, 0, 0));
    key(KEY_RIGHT); key(' ');
    assert(lcd_get(lcd, 1, 0));
    key(' ');
    assert(!lcd_get(lcd, 1, 0));
    key('d'); assert(lcd_get(lcd, 1, 0)); key('e'); assert(!lcd_get(lcd, 1, 0));
    key('p'); assert(pen == 1);
    key(KEY_DOWN); key(KEY_DOWN); key(KEY_LEFT);
    assert(lcd_get(lcd, 1, 1) && lcd_get(lcd, 1, 2) && lcd_get(lcd, 0, 2));
    key('p'); assert(pen == 2);
    key(KEY_UP);
    key('p'); assert(pen == 0);
    key(KEY_LEFT); assert(lcd_cx == 47);
    /* Live push can be turned off and on; a busy LCD is retried. */
    fake_time += 500; step(0, 0, 0, 0); /* flush any pending push first */
    screen_pushes = 0;
    click_id(C_L_LIVE);
    assert(!lcd_live);
    key(' ');
    fake_time += 500; step(0, 0, 0, 0);
    assert(screen_pushes == 0);
    click_id(C_L_SEND);
    assert(screen_pushes == 1);
    click_id(C_L_LIVE);
    assert(lcd_live && !lcd_pending && screen_pushes == 2 && !memcmp(screen_last, lcd, LCD_BYTES)); /* pushes on enabling */
    screen_fail = DC_VMUF_BUSY;
    key(' ');
    fake_time += 500; step(0, 0, 0, 0);
    assert(lcd_pending && screen_pushes == 2); /* a busy frame is retried, not dropped */
    screen_fail = 0;
    fake_time += 500; step(0, 0, 0, 0);
    assert(!lcd_pending && screen_pushes == 3 && !memcmp(screen_last, lcd, LCD_BYTES));
    screen_fail = DC_VMUF_UNSUPPORTED;
    click_id(C_L_SEND);
    assert(strstr(message, "no LCD"));
    screen_fail = 0;
    /* Save as BMP and raw, reload both. */
    for (int i = 0; i < LCD_BYTES; i++) lcd[i] = (uint8_t)(i * 5 + 3);
    uint8_t original[LCD_BYTES];
    memcpy(original, lcd, sizeof(original));
    snprintf(scripted_path, sizeof(scripted_path), "C:\\");
    snprintf(scripted_name, sizeof(scripted_name), "PIC.BMP");
    click_id(C_L_SAVE);
    assert(!lcd_dirty && host_read("C:\\PIC.BMP", b, 4096) == LCD_BMP_BYTES && !memcmp(b, "BM", 2));
    snprintf(scripted_name, sizeof(scripted_name), "PIC.LCD");
    click_id(C_L_SAVE);
    assert(host_read("C:\\PIC.LCD", b, 4096) == LCD_BYTES && !memcmp(b, original, LCD_BYTES));
    snprintf(scripted_name, sizeof(scripted_name), "PIC.PNG");
    click_id(C_L_SAVE);
    assert(strstr(message, ".BMP or .LCD"));
    snprintf(scripted_name, sizeof(scripted_name), "PIC.BMP");
    confirm_answer = 0; confirm_calls = 0;
    click_id(C_L_SAVE);
    assert(confirm_calls == 1 && strstr(last_confirm, "already exists"));
    snprintf(scripted_path, sizeof(scripted_path), "D:\\");
    click_id(C_L_SAVE);
    assert(strstr(message, "read-only"));
    memset(lcd, 0, sizeof(lcd));
    snprintf(scripted_path, sizeof(scripted_path), "C:\\PIC.BMP");
    click_id(C_L_LOAD);
    assert(!memcmp(lcd, original, LCD_BYTES) && !lcd_dirty);
    memset(lcd, 0, sizeof(lcd));
    snprintf(scripted_path, sizeof(scripted_path), "C:\\PIC.LCD");
    click_id(C_L_LOAD);
    assert(!memcmp(lcd, original, LCD_BYTES));
    host_write("C:\\JUNK.BMP", "not a bitmap at all", 19);
    snprintf(scripted_path, sizeof(scripted_path), "C:\\JUNK.BMP");
    click_id(C_L_LOAD);
    assert(message_colour == RED && !memcmp(lcd, original, LCD_BYTES));
    /* Leaving with an unsaved bitmap asks before quitting. */
    lcd_dirty = 1;
    click_id(C_L_BACK);
    assert(view == VIEW_FILES);
    confirm_answer = 0; confirm_calls = 0;
    key(27);
    assert(!quitting && confirm_calls == 1);
    confirm_answer = 1;
    key(27);
    assert(quitting);
    quitting = 0; lcd_dirty = 0;

    /* ---- Icon editor on a real VMS layout ---- */
    unsigned char vmsfile[1536];
    memset(vmsfile, 0, sizeof(vmsfile));
    memcpy(vmsfile, "Icon test       ", 16);
    memcpy(vmsfile + 16, "Editable save icon for tests    ", 32);
    memcpy(vmsfile + 48, "ICONTEST\0\0\0\0\0\0\0\0", 16);
    vmsfile[64] = 2; vmsfile[66] = 5;
    vmsfile[72] = 300 & 255; vmsfile[73] = 300 >> 8;
    for (int i = 0; i < 16; i++) { vmsfile[96 + 2 * i] = (i * 0x11) & 255; vmsfile[97 + 2 * i] = 0xf0 | (i & 15); }
    fill(vmsfile + VMS_HEADER + 2 * 512, 300, 77);
    for (int i = 0; i < 2 * 512; i++) vmsfile[VMS_HEADER + i] = (i / 16) % 2 ? 0x12 : 0x21;
    struct vms_info tmp = {.total = VMS_HEADER + 1024 + 300};
    vms_seal(vmsfile, &tmp);
    assert(vms_parse(vmsfile, sizeof(vmsfile), &tmp) == VMS_OK);
    format_card();
    assert(dc_vmuf_write(&backend, &device, "MYSAVE.001", vmsfile, sizeof(vmsfile), 0) == 1536);
    add_card_file("PLAIN.TXT", a, 100, 0, 0);
    reload();
    /* A file that is not a VMS save is refused with a reason. */
    select_named("PLAIN.TXT");
    memcpy(snapshot, image, sizeof(image));
    click_id(C_ICON);
    assert(message_colour == RED && strstr(message, "PLAIN.TXT: "));
    assert(view == VIEW_FILES && !memcmp(snapshot, image, sizeof(image)));
    select_named("MYSAVE.001");
    click_id(C_ICON);
    assert(view == VIEW_ICON && vms.icons == 2 && icon_bytes == 1536 && !strcmp(icon_name, "MYSAVE.001"));
    redraw();
    assert(palette_swapped);
    /* Keyboard: move, pick colour, paint. */
    icon_cx = icon_cy = 0;
    int original_pixel = vms_pixel(file_buffer, 0, 3, 2);
    key(KEY_RIGHT); key(KEY_RIGHT); key(KEY_RIGHT); key(KEY_DOWN); key(KEY_DOWN);
    assert(icon_cx == 3 && icon_cy == 2);
    key('.');
    int colour = icon_colour;
    key(' ');
    assert(vms_pixel(file_buffer, 0, 3, 2) == colour && icon_dirty && colour != original_pixel);
    /* Mouse: paint a stroke, pick with the right button. */
    step(0, GX + 10 * CELL + 1, GY + 10 * CELL + 1, 1);
    step(0, GX + 15 * CELL + 1, GY + 10 * CELL + 1, 1);
    step(0, GX + 15 * CELL + 1, GY + 10 * CELL + 1, 0);
    for (int x = 10; x <= 15; x++) assert(vms_pixel(file_buffer, 0, x, 10) == colour);
    step(0, GX + 2 * CELL + 1, GY + 1 * CELL + 1, 2);
    step(0, GX + 2 * CELL + 1, GY + 1 * CELL + 1, 0);
    assert(icon_colour == vms_pixel(file_buffer, 0, 2, 1));
    /* Palette channel editing and frames. */
    unsigned old_entry = vms_palette(file_buffer, icon_colour);
    key('B');
    assert((old_entry & 15) == 15 ? vms_palette(file_buffer, icon_colour) == old_entry
                                  : vms_palette(file_buffer, icon_colour) == old_entry + 1);
    key('b'); key('b');
    key(']');
    assert(icon_frame == 1);
    key(']');
    assert(icon_frame == 0);
    key('[');
    assert(icon_frame == 1);
    key(' ');
    assert(vms_pixel(file_buffer, 1, icon_cx, icon_cy) == icon_colour);
    /* Undo, flips. */
    click_id(C_I_FLIPH);
    click_id(C_I_FLIPV);
    click_id(C_I_FLIPV);
    click_id(C_I_FLIPH);
    /* Save: confirmation first; the card gets a valid, minimally-changed file. */
    confirm_answer = 0; confirm_calls = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_I_SAVE);
    assert(confirm_calls == 1 && strstr(last_confirm, "Write the edited icon") && !memcmp(snapshot, image, sizeof(image)));
    confirm_answer = 1;
    click_id(C_I_SAVE);
    assert(!icon_dirty && strstr(message, "verified"));
    unsigned char saved[1536];
    assert(fake_file_read(0, 0, "MYSAVE.001", saved, sizeof(saved)) == 1536);
    struct vms_info after_info;
    assert(vms_parse(saved, sizeof(saved), &after_info) == VMS_OK);
    assert(!memcmp(saved, vmsfile, 64) && !memcmp(saved + 64, vmsfile + 64, 6));          /* text, ids, counts */
    assert(!memcmp(saved + 72, vmsfile + 72, 24));                                            /* data length, reserved */
    assert(!memcmp(saved + VMS_HEADER + 1024, vmsfile + VMS_HEADER + 1024, 384));             /* data and padding untouched */
    assert(memcmp(saved + VMS_HEADER, vmsfile + VMS_HEADER, 1024));                           /* icons changed */
    assert(vms_pixel(saved, 0, 3, 2) == colour);
    /* Leaving with unsaved changes asks; declining keeps editing. */
    icon_frame = 0;
    key('.');
    key(' ');
    assert(icon_dirty);
    confirm_answer = 0; confirm_calls = 0;
    key(27);
    assert(view == VIEW_ICON && confirm_calls == 1);
    confirm_answer = 1;
    key(27);
    assert(view == VIEW_FILES && !palette_swapped);
    /* A failed icon write keeps the edit for a retry. */
    click_id(C_ICON);
    key('.');
    key(' ');
    allow_writes = 0;
    memcpy(snapshot, image, sizeof(image));
    click_id(C_I_SAVE);
    allow_writes = 1;
    assert(icon_dirty && message_colour == RED && !memcmp(snapshot, image, sizeof(image)));
    click_id(C_I_BACK);
    assert(view == VIEW_FILES);
    /* The palette slots map every colour, including when a save uses all sixteen. */
    click_id(C_ICON);
    assert(view == VIEW_ICON);
    for (int i = 0; i < 16; i++) icon_slot[i] = -1;
    apply_icon_palette();
    for (int i = 0; i < 16; i++) assert(icon_slot[i] >= 0 && icon_slot[i] < 16);
    icon_leave();
    chdir("/");
    { char cmd[64]; snprintf(cmd, sizeof(cmd), "rm -rf %s", tmpdir); assert(!system(cmd)); }
    puts("VMU editor GUI logic against the file service: PASS");
    return 0;
}
