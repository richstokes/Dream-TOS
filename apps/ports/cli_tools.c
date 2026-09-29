/* Small native command-line utilities for EmuCON. GPL-2.0-or-later.
 * Built as separate .TTP programs; host tests use exactly the same code.
 * Text filters normalize CRLF; wc, cksum and hexdump inspect original bytes. */
#include "app.h"
#include "dreamcast/system_info.h"
#include "tinyexpr.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#ifndef CLI_TOOL
#define CLI_TOOL "uname"
#endif
#define IS_TOOL(name) (__builtin_strcmp(CLI_TOOL, name) == 0)
#define LINE_LIMIT 65536u
#define TEXT_LIMIT (1024u * 1024u)
#define LINE_COUNT 10000u
static int report(const char *path)
{
    fprintf(stderr, "%s: %s: %s\n", CLI_TOOL, path, strerror(errno));
    return 2;
}
static FILE *input(const char *path)
{
    return strcmp(path, "-") ? fopen(path, "rb") : stdin;
}
static void close_input(FILE *f)
{
    if (f != stdin) fclose(f);
}
/* Returns 1 for a line (including an unterminated final line), 0 for EOF,
 * -1 for errors. Never silently split long lines into independent records. */
static int line_read(FILE *f, char **out)
{
    size_t len = 0, cap = 256;
    char *line = malloc(cap);
    if (!line) return -1;
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {
        if (len == LINE_LIMIT) { errno = EOVERFLOW; free(line); return -1; }
        if (len + 1 == cap) {
            char *next = realloc(line, cap * 2);
            if (!next) { free(line); return -1; }
            line = next; cap *= 2;
        }
        /* These are text filters: reject embedded NUL rather than truncate. */
        if (!c) { errno = EILSEQ; free(line); return -1; }
        line[len++] = c;
    }
    if (ferror(f)) { free(line); return -1; }
    if (c == EOF && !len) { free(line); return 0; }
    if (len && line[len - 1] == '\r') len--;
    line[len] = 0;
    *out = line;
    return 1;
}
static int number(const char *s, unsigned *value)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(s, &end, 10);
    if (!*s || *s == '-' || *end || errno || n > LINE_COUNT) return 0;
    *value = n;
    return 1;
}
static int usage(void)
{
    static const char *const help[] = {
        "grep [-i] [-n] [-v] [--] LITERAL FILE... (literal text, no regex)",
        "wc [-lwc] [--] FILE... (lines, words, bytes)",
        "head [-n 0..10000] [--] FILE... (default 10 lines)",
        "tail [-n 0..10000] [--] FILE... (default 10 lines)",
        "sort [-r] [-u] [--] FILE... (byte ordering, merged input)",
        "cksum [--] FILE... (POSIX CRC and byte count)",
        "hexdump [--] FILE... (offset, hex bytes and ASCII)",
        "date (current local RTC date/time)",
        "df [C:|D:] (mounted drive capacity and free bytes)",
        "free (GEMDOS memory and system heap usage)",
        "uname [-a] (OS, architecture and optional system details)",
        "expr EXPRESSION (tinyexpr arithmetic, e.g. expr sqrt(144)+2^3)",
        NULL
    };
    for (int i = 0; help[i]; i++)
        if (!strncmp(help[i], CLI_TOOL, strlen(CLI_TOOL)) && help[i][strlen(CLI_TOOL)] == ' ')
            puts(help[i]);
    puts("Text tools require filenames; use - for keyboard input (Ctrl+D ends input).");
    return 0;
}
static int contains(const char *s, const char *pattern, int insensitive)
{
    if (!*pattern) return 1;
    for (; *s; s++) {
        size_t i = 0;
        while (pattern[i] && s[i] && (insensitive ?
               tolower((unsigned char)s[i]) == tolower((unsigned char)pattern[i]) :
               s[i] == pattern[i])) i++;
        if (!pattern[i]) return 1;
    }
    return 0;
}
static int grep_main(int argc, char **argv)
{
    int insensitive = 0, numbered = 0, inverted = 0, arg = 1, matches = 0, errors = 0;
    for (; arg < argc && argv[arg][0] == '-' && argv[arg][1]; arg++) {
        if (!strcmp(argv[arg], "--")) { arg++; break; }
        for (const char *p = argv[arg] + 1; *p; p++) {
            if (*p == 'i') insensitive = 1;
            else if (*p == 'n') numbered = 1;
            else if (*p == 'v') inverted = 1;
            else { usage(); return 2; }
        }
    }
    if (argc - arg < 2) { usage(); return 2; }
    const char *pattern = argv[arg++];
    int multi = argc - arg > 1;
    for (; arg < argc; arg++) {
        FILE *f = input(argv[arg]);
        if (!f) { errors = report(argv[arg]); continue; }
        char *line;
        unsigned long n = 0;
        int rc;
        while ((rc = line_read(f, &line)) > 0) {
            n++;
            if (contains(line, pattern, insensitive) != inverted) {
                matches = 1;
                if (multi) printf("%s:", argv[arg]);
                if (numbered) printf("%lu:", n);
                puts(line);
            }
            free(line);
        }
        if (rc < 0) errors = report(argv[arg]);
        close_input(f);
    }
    return errors ? errors : !matches;
}
static uint32_t crc_byte(uint32_t crc, unsigned byte)
{
    crc ^= (uint32_t)byte << 24;
    for (int i = 0; i < 8; i++)
        crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    return crc;
}
static void counts(const unsigned long *n, int flags, const char *name)
{
    for (int i = 0; i < 3; i++)
        if (flags & (1 << i)) printf("%lu ", n[i]);
    puts(name);
}
static int bytes_main(int argc, char **argv)
{
    int arg = 1, flags = 0, errors = 0;
    int wc = IS_TOOL("wc"), crc_mode = IS_TOOL("cksum");
    for (; arg < argc && argv[arg][0] == '-' && argv[arg][1]; arg++) {
        if (!strcmp(argv[arg], "--")) { arg++; break; }
        if (!wc) { usage(); return 2; }
        for (const char *p = argv[arg] + 1; *p; p++) {
            if (*p == 'l') flags |= 1;
            else if (*p == 'w') flags |= 2;
            else if (*p == 'c') flags |= 4;
            else { usage(); return 2; }
        }
    }
    if (!flags) flags = 7;
    if (arg == argc) { usage(); return 2; }
    unsigned long total[3] = {0};
    int multi = argc - arg > 1;
    for (; arg < argc; arg++) {
        FILE *f = input(argv[arg]);
        if (!f) { errors = report(argv[arg]); continue; }
        unsigned long count[3] = {0};
        uint32_t crc = 0;
        int inword = 0;
        unsigned char buffer[4096];
        size_t n;
        if (!wc && !crc_mode && multi) printf("==> %s <==\n", argv[arg]);
        while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0) {
            if (!wc && !crc_mode) {
                for (size_t off = 0; off < n; off += 16) {
                    printf("%08lx  ", count[2] + (unsigned long)off);
                    for (size_t j = 0; j < 16; j++) {
                        if (off + j < n) printf("%02x ", buffer[off + j]); else printf("   ");
                    }
                    printf(" |");
                    for (size_t j = off; j < n && j < off + 16; j++)
                        putchar(buffer[j] >= 32 && buffer[j] < 127 ? buffer[j] : '.');
                    puts("|");
                }
            }
            for (size_t i = 0; i < n; i++) {
                int space = isspace(buffer[i]);
                if (buffer[i] == '\n') count[0]++;
                if (!space && !inword) count[1]++;
                inword = !space;
                if (crc_mode) crc = crc_byte(crc, buffer[i]);
            }
            count[2] += n;
        }
        if (ferror(f)) errors = report(argv[arg]);
        else if (wc) counts(count, flags, argv[arg]);
        else if (crc_mode) {
            for (unsigned long len = count[2]; len; len >>= 8) crc = crc_byte(crc, len & 255);
            printf("%lu %lu %s\n", (unsigned long)~crc, count[2], argv[arg]);
        }
        for (int i = 0; i < 3; i++) total[i] += count[i];
        close_input(f);
    }
    if (wc && multi) counts(total, flags, "total");
    return errors;
}
static int compare_lines(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}
static int lines_main(int argc, char **argv)
{
    unsigned limit = 10, count = 0, next = 0;
    int sorting = IS_TOOL("sort"), tail = IS_TOOL("tail");
    int reverse = 0, unique = 0, arg = 1, errors = 0;
    size_t used = 0;
    for (; arg < argc && argv[arg][0] == '-' && argv[arg][1]; arg++) {
        if (!strcmp(argv[arg], "--")) { arg++; break; }
        if (!sorting && !strcmp(argv[arg], "-n") && arg + 1 < argc) {
            if (!number(argv[++arg], &limit)) { usage(); return 2; }
        } else if (sorting && !strcmp(argv[arg], "-r")) reverse = 1;
        else if (sorting && !strcmp(argv[arg], "-u")) unique = 1;
        else { usage(); return 2; }
    }
    if (arg == argc) { usage(); return 2; }
    if (sorting) limit = LINE_COUNT;
    char **lines = calloc(limit ? limit : 1, sizeof(*lines));
    if (!lines) return report("memory");
    int multi = argc - arg > 1;
    for (; arg < argc; arg++) {
        FILE *f = input(argv[arg]);
        if (!f) { errors = report(argv[arg]); continue; }
        if (!sorting && multi) printf("==> %s <==\n", argv[arg]);
        char *line;
        int rc = 0;
        unsigned emitted = 0;
        while ((sorting || tail || emitted < limit) && (rc = line_read(f, &line)) > 0) {
            if (!sorting && !tail) { puts(line); free(line); emitted++; continue; }
            if (!limit) { free(line); continue; }
            if (!sorting && count == limit) {
                used -= strlen(lines[next]) + 1;
                free(lines[next]); lines[next] = NULL;
            }
            size_t bytes = strlen(line) + 1;
            if ((sorting && count == limit) || bytes > TEXT_LIMIT - used) {
                free(line); errno = EOVERFLOW; rc = -1; break;
            }
            lines[next] = line; used += bytes;
            next = (next + 1) % limit;
            if (count < limit) count++;
        }
        if (rc < 0) errors = report(argv[arg]);
        close_input(f);
        if (tail) {
            for (unsigned i = 0; i < count; i++) {
                unsigned index = (count == limit ? next + i : i) % limit;
                if (lines[index]) { puts(lines[index]); free(lines[index]); lines[index] = NULL; }
            }
            count = next = 0; used = 0;
        }
    }
    if (sorting) {
        qsort(lines, count, sizeof(*lines), compare_lines);
        for (unsigned i = 0; i < count; i++) {
            unsigned index = reverse ? count - 1 - i : i;
            if (!unique || !i || strcmp(lines[index], lines[reverse ? index + 1 : index - 1]))
                puts(lines[index]);
        }
        for (unsigned i = 0; i < count; i++) free(lines[i]);
    }
    free(lines);
    return errors;
}
static int system_main(int argc, char **argv)
{
    struct dc_system_info info;
    if (dc_os->size < offsetof(struct dc_native_api, system_info) + sizeof(dc_os->system_info) ||
        !dc_os->system_info || dc_os->system_info(&info, sizeof(info)) < 0) {
        fputs("System information is unavailable\n", stderr); return 2;
    }
    if (IS_TOOL("date")) {
        if (argc != 1) { usage(); return 2; }
        unsigned d = dc_os->gemdos(0x2a), t = dc_os->gemdos(0x2c);
        printf("%04u-%02u-%02u %02u:%02u:%02u\n", 1980 + (d >> 9),
               (d >> 5) & 15, d & 31, t >> 11, (t >> 5) & 63, (t & 31) * 2);
    } else if (IS_TOOL("uname")) {
        if (argc > 2 || (argc == 2 && strcmp(argv[1], "-a"))) { usage(); return 2; }
        printf("EmuTOS %s Sega Dreamcast SH-4\n", info.os_version);
        if (argc == 2) printf("KallistiOS %s; uptime %lu seconds; %lux%lu\n", info.kos_version,
            (unsigned long)info.uptime_seconds, (unsigned long)info.video_width, (unsigned long)info.video_height);
    } else if (IS_TOOL("free")) {
        if (argc != 1) { usage(); return 2; }
        printf("GEMDOS total: %lu bytes\nGEMDOS free: %lu bytes\nLargest block: %lu bytes\n"
               "System RAM: %lu bytes\nKOS heap used: %lu bytes\n",
               (unsigned long)info.gem_pool_bytes, (unsigned long)info.gem_free_bytes,
               (unsigned long)info.gem_largest_bytes, (unsigned long)info.ram_bytes,
               (unsigned long)info.heap_used_bytes);
    } else {
        int drive = -1;
        if (argc > 2) { usage(); return 2; }
        if (argc == 2) {
            if (strlen(argv[1]) != 2 || argv[1][1] != ':' || !isalpha((unsigned char)argv[1][0])) {
                usage(); return 2;
            }
            drive = toupper((unsigned char)argv[1][0]) - 'A';
            if (!(info.drive_mask & (1u << drive))) { fputs("Drive is not mounted\n", stderr); return 2; }
        }
        puts("Drive   Total bytes   Free bytes   Storage");
        for (int d = 0; d < 26; d++) if ((drive < 0 || d == drive) && (info.drive_mask & (1u << d))) {
            long space[4];
            long result = dc_os->gemdos(0x36, space, d + 1);
            if (result < 0) { fprintf(stderr, "df: %c: error %ld\n", 'A' + d, result); return 2; }
            printf("%c: %15lu %12lu   %s%s\n", 'A' + d, (unsigned long)(space[1] * space[2] * space[3]),
                (unsigned long)(space[0] * space[2] * space[3]), (info.readonly_mask & (1u << d)) ? "read-only " : "writable ",
                (info.volatile_mask & (1u << d)) ? "RAM (lost at reset)" : (info.readonly_mask & (1u << d)) ? "disc" : "SD card");
        }
    }
    return 0;
}
int app_main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) return usage();
    int result;
    if (IS_TOOL("grep")) result = grep_main(argc, argv);
    else if (IS_TOOL("wc") || IS_TOOL("cksum") || IS_TOOL("hexdump"))
        result = bytes_main(argc, argv);
    else if (IS_TOOL("head") || IS_TOOL("tail") || IS_TOOL("sort"))
        result = lines_main(argc, argv);
    else if (IS_TOOL("expr")) {
        char expression[128] = "";
        for (int i = 1; i < argc; i++) {
            if (strlen(expression) + strlen(argv[i]) + 2 > sizeof(expression)) { usage(); return 2; }
            if (i > 1) strcat(expression, " ");
            strcat(expression, argv[i]);
        }
        int error;
        double value = te_interp(expression, &error);
        if (argc < 2 || error || !isfinite(value)) {
            fputs("expr: invalid expression or arithmetic domain\n", stderr); return 2;
        }
        printf("%.12g\n", value); result = 0;
    } else result = system_main(argc, argv);
    if (fflush(stdout) || ferror(stdout)) return report("stdout");
    return result;
}
