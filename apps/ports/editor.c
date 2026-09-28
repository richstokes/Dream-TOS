/* Native GEM frontend to Kilo (BSD-2-Clause engine). GPL-2.0-or-later frontend. */
#include "app.h"
#define DC_NATIVE
#ifndef APP_HOST_TEST
#define getline __getline
#endif
#include "../vendor/kilo/kilo.c"
static int pending, has_path;
static const size_t max_text = 65536;
int editorReadKey(int fd)
{
    (void)fd;
    if (pending) {
        int k = pending;
        pending = 0;
        return k;
    }
    int k = app_key();
    switch (k >> 8) {
    case 0x48:
        return ARROW_UP;
    case 0x50:
        return ARROW_DOWN;
    case 0x4b:
        return ARROW_LEFT;
    case 0x4d:
        return ARROW_RIGHT;
    case 0x47:
        return HOME_KEY;
    case 0x4f:
        return END_KEY;
    case 0x49:
        return PAGE_UP;
    case 0x51:
        return PAGE_DOWN;
    case 0x53:
        return DEL_KEY;
    default:
        return k & 255;
    }
}
void editorRefreshScreen(void)
{
    app_clear(0);
    char buf[100];
    snprintf(buf, sizeof(buf), "%.62s %s", E.filename ? E.filename : "Untitled",
             E.dirty ? "[modified]" : "");
    app_text(8, 42, buf, 1);
    app_clip(8, 48, 624, 352);
    for (int y = 0; y < E.screenrows; y++) {
        int r = y + E.rowoff;
        if (r >= E.numrows)
            break;
        erow *row = &E.row[r];
        int n = row->rsize - E.coloff;
        if (n < 0)
            n = 0;
        if (n > 78)
            n = 78;
        memcpy(buf, row->render + (n ? E.coloff : 0), n);
        buf[n] = 0;
        /* Highlighting state remains in Kilo for search and C syntax. */
        for (int x = 0; x < n; x++) {
            char ch[2] = {buf[x], 0};
            int hl = row->hl[E.coloff + x];
            app_text(8 + 8 * x, 61 + 16 * y, ch,
                     hl == HL_MATCH    ? 2
                     : hl == HL_STRING ? 3
                     : hl == HL_NUMBER ? 4
                                       : 1);
        }
    }
    int cx = E.cx;
    int r = E.rowoff + E.cy;
    if (r < E.numrows) {
        cx = 0;
        for (int i = 0; i < E.cx + E.coloff && i < E.row[r].size; i++)
            cx += E.row[r].chars[i] == '\t' ? 8 - (cx % 8) : 1;
        cx -= E.coloff;
    }
    app_line(8 + cx * 8, 63 + E.cy * 16, 15 + cx * 8, 63 + E.cy * 16, 1);
    app_unclip();
    app_text(8, 426, E.statusmsg, 1);
    app_status("^O:open ^N:new ^S:save ^A:save as ^F:find ^Q:quit");
}
static int save_as(int force)
{
    char path[80];
    snprintf(path, sizeof(path), "%s", E.filename ? E.filename : "C:\\NOTES.TXT");
    if (force || !has_path || !E.filename || (path[0] != 'C' && path[0] != 'c') || path[1] != ':') {
        strcpy(path, "C:\\NOTES.TXT");
        if (!app_prompt("Save on RAM disk (C:\\NAME.TXT):", path, sizeof(path)))
            return 1;
        if ((path[0] != 'C' && path[0] != 'c') || path[1] != ':') {
            editorSetStatusMessage("Save on C:; disc D: is read-only.");
            return 1;
        }
        FILE *exists = fopen(path, "rb");
        if (exists) {
            fclose(exists);
            ai[0] = 2;
            aa[0] = (intptr_t)"[2][Replace existing file?][Replace|Cancel]";
            aes_call(52, 1, 1, 1);
            if (ao[0] != 1)
                return 1;
        }
    }
    int len;
    char *buf = editorRowsToString(&len);
    FILE *f = fopen(path, "wb");
    int ok = 0;
    if (f) {
        ok = fwrite(buf, 1, len, f) == (size_t)len;
        if (fclose(f))
            ok = 0;
    }
    free(buf);
    if (ok) {
        free(E.filename);
        E.filename = strdup(path);
        has_path = 1;
        E.dirty = 0;
        editorSetStatusMessage("Saved %d bytes on C: (lost at reset)", len);
    } else
        editorSetStatusMessage("Save failed: %s", strerror(errno));
    return !ok;
}
int editorSave(void)
{
    return save_as(0);
}
static int discard(void)
{
    if (!E.dirty)
        return 1;
    ai[0] = 2;
    aa[0] = (intptr_t)"[2][Discard unsaved changes?][Discard|Cancel]";
    aes_call(52, 1, 1, 1);
    return ao[0] == 1;
}
static void clear_buffer(void)
{
    for (int i = 0; i < E.numrows; i++)
        editorFreeRow(&E.row[i]);
    free(E.row);
    free(E.filename);
    has_path = 0;
    memset(&E, 0, sizeof(E));
    E.screenrows = 22;
    E.screencols = 78;
}
static void load_file(const char *path)
{
    /* Bound input before handing it to upstream Kilo, whose allocations are
     * unchecked. Keep the current buffer intact on failed/oversized reads. */
    FILE *f = fopen(path, "rb");
    if (!f) {
        editorSetStatusMessage("Cannot open %s", path);
        return;
    }
    size_t bytes = 0, lines = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        bytes++;
        if (c == '\n')
            lines++;
    }
    int failed = ferror(f);
    fclose(f);
    if (failed || bytes > max_text || lines >= 1024) {
        editorSetStatusMessage("File too large (64 KiB / 1023 lines maximum) or unreadable");
        return;
    }
    clear_buffer();
    editorSelectSyntaxHighlight((char *)path);
    editorOpen((char *)path);
    has_path = 1;
    editorSetStatusMessage("Opened %s", path);
}
int app_main(int argc, char **argv)
{
    if (!app_begin("KILO - native GEM text editor"))
        return 1;
    atexit(app_end);
    clear_buffer();
    if (argc > 1)
        load_file(argv[1]);
    else {
        E.filename = strdup("C:\\NOTES.TXT");
        editorSetStatusMessage("New document. C: is temporary storage; reset erases it.");
    }
    for (;;) {
        editorRefreshScreen();
        int key = editorReadKey(0);
        if (key == 15) {
            char path[80] = "D:\\README.TXT";
            if (discard() && app_prompt("Open text file:", path, sizeof(path)))
                load_file(path);
        } else if (key == 14) {
            if (discard()) {
                clear_buffer();
                E.filename = strdup("C:\\NOTES.TXT");
            }
        } else if (key == 1)
            save_as(1);
        else if (key == HOME_KEY) {
            E.cx = E.coloff = 0;
        } else if (key == END_KEY) {
            int r = E.rowoff + E.cy;
            int len = r < E.numrows ? E.row[r].size : 0;
            E.coloff = len > 77 ? len - 77 : 0;
            E.cx = len - E.coloff;
        } else if (key == DEL_KEY) {
            editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
        } else {
            size_t n = 0;
            for (int i = 0; i < E.numrows; i++)
                n += E.row[i].size + 1;
            if ((key == ENTER || (key >= 32 && key < 127)) && (n >= max_text || E.numrows >= 1023))
                editorSetStatusMessage("Document limit reached; save before continuing.");
            else {
                pending = key;
                editorProcessKeypress(0);
            }
        }
    }
}
