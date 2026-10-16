/* Fake Saturn for ftx tests: an LD_PRELOAD library that replaces libftdi and answers
 * SRL_HostIO remote I/O commands (LIST, REMOVE, CRC, UPLOAD, MKDIR, RENAME, DOWNLOAD)
 * from the host folder $FAKE_SD, using the firmware's reply formats. Like the firmware,
 * it cuts a LIST reply at 1024 bytes and still reports OK.
 *
 * Knobs:
 *   FAKE_CUT=N             a DOWNLOAD stops after N data bytes (reads fail from then on)
 *   FAKE_SINK=1            swallow every write and never answer (for interrupt tests)
 *   FAKE_WRITE_DELAY_US=N  sleep N microseconds in every write
 *   FAKE_ECHO_DELAY_MS=N   act as a plain target for ftx -g: echo every write back N ms later
 *   FAKE_READ_FAIL=1       every read fails, like a USB handle that died
 *
 * Paths starting with "sdraw:" are logged as "[fakesat] RAW ..." instead of touching sectors.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct ftdi_context;

#define MAX_PATH 75000 /* holds any 16-bit-length command argument */

static unsigned char in[70000], out[1 << 22];
static size_t in_len, out_len, out_pos, cut_at = (size_t)-1;
static char up_path[MAX_PATH];
static unsigned char up_buf[1 << 22];
static size_t up_len, up_need, up_size;
static unsigned char echo_buf[1 << 16];
static size_t echo_len;
static struct timespec echo_ready;

static long ms_until(const struct timespec *t)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (t->tv_sec - now.tv_sec) * 1000 + (t->tv_nsec - now.tv_nsec) / 1000000;
}

/* Echoed bytes "arrive" in the receive buffer once their delay has passed. */
static void deliver_echo(void)
{
    if (echo_len && ms_until(&echo_ready) <= 0)
    {
        memcpy(out + out_len, echo_buf, echo_len);
        out_len += echo_len;
        echo_len = 0;
    }
}

static unsigned char crc8(const unsigned char *p, size_t n)
{
    unsigned char c = 0;
    while (n--)
    {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (c & 0x80) ? (unsigned char)((c << 1) ^ 0x07) : (unsigned char)(c << 1);
    }
    return c;
}

static void reply(int status, const void *payload, size_t len)
{
    unsigned char h[7] = {'S', 'R', 'L', '1', (unsigned char)status, (unsigned char)(len >> 8), (unsigned char)len};
    memcpy(out + out_len, h, 7);
    memcpy(out + out_len + 7, payload, len);
    out_len += 7 + len;
}

static size_t read_file(const char *path, unsigned char *data, size_t cap, int *ok)
{
    FILE *f = fopen(path, "rb");
    *ok = f != NULL;
    if (!f) return 0;
    size_t n = fread(data, 1, cap, f);
    fclose(f);
    return n;
}

static void handle(int cmd, const char *arg)
{
    static unsigned char data[1 << 22];
    char path[MAX_PATH];
    int ok;
    snprintf(path, sizeof path, "%s%s", getenv("FAKE_SD"), arg);
    fprintf(stderr, "[fakesat] cmd=%d arg='%s'\n", cmd, arg);
    if (!strncmp(arg, "sdraw:", 6) && (cmd == 2 || cmd == 4)) fprintf(stderr, "[fakesat] RAW %s\n", arg);

    if (cmd == 1) /* LIST "-l <dir>" */
    {
        snprintf(path, sizeof path, "%s%s", getenv("FAKE_SD"), arg + 3);
        DIR *d = opendir(path);
        if (!d) { reply(1, "", 0); return; }
        static char text[60000];
        size_t n = (size_t)snprintf(text, sizeof text, "[DoList] Listing: %s\n", arg + 3);
        for (struct dirent *e; (e = readdir(d));)
        {
            char full[2 * MAX_PATH];
            struct stat st;
            snprintf(full, sizeof full, "%s/%s", path, e->d_name);
            if (stat(full, &st)) continue;
            char when[32];
            strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
            n += (size_t)snprintf(text + n, sizeof text - n, "  %s %10lu %s %s\n", S_ISDIR(st.st_mode) ? "[D]" : "[F]",
                                  S_ISDIR(st.st_mode) ? 0UL : (unsigned long)st.st_size, when, e->d_name);
        }
        closedir(d);
        if (n > 1023) { n = 1023; text[n++] = 0; }
        reply(0, text, n);
        reply(0, "", 0); /* end-of-listing sentinel */
    }
    else if (cmd == 2) /* REMOVE */
    {
        if (!strncmp(arg, "sdraw:", 6)) { reply(0, "", 0); return; }
        reply(remove(path) ? 1 : 0, "", 0);
    }
    else if (cmd == 3) /* CRC */
    {
        size_t n = read_file(path, data, sizeof data, &ok);
        if (!ok) { reply(1, "", 0); return; }
        char text[MAX_PATH + 64];
        int len = snprintf(text, sizeof text, "[DoCrc] %s CRC-8 = 0x%02X\n", arg, crc8(data, n));
        reply(0, text, (size_t)len);
    }
    else if (cmd == 4) /* UPLOAD, then size(4) + data + crc(1) from the host, answered by one raw byte */
    {
        snprintf(up_path, sizeof up_path, "%s", path);
        up_len = 0;
        up_need = 4;
        up_size = (size_t)-1;
        reply(0, "", 0);
    }
    else if (cmd == 6) /* RMDIR: FatFs f_unlink, which only removes empty directories */
        reply(remove(path) ? 1 : 0, "", 0);
    else if (cmd == 5) /* MKDIR */
        reply(mkdir(path, 0755) ? 1 : 0, "", 0);
    else if (cmd == 7) /* RENAME "old\0new" */
    {
        char to[MAX_PATH];
        snprintf(to, sizeof to, "%s%s", getenv("FAKE_SD"), arg + strlen(arg) + 1);
        reply(rename(path, to) ? 1 : 0, "", 0);
    }
    else if (cmd == 8) /* DOWNLOAD */
    {
        size_t n = read_file(path, data, sizeof data, &ok);
        if (!ok) { reply(1, "", 0); return; }
        unsigned char sz[4] = {(unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n};
        reply(0, sz, 4);
        if (getenv("FAKE_CUT")) cut_at = out_len + (size_t)atol(getenv("FAKE_CUT"));
        memcpy(out + out_len, data, n);
        out_len += n;
        out[out_len++] = crc8(data, n);
    }
    else
        reply(2, "", 0); /* unsupported */
}

int ftdi_write_data(struct ftdi_context *f, const unsigned char *buf, int size)
{
    (void)f;
    if (getenv("FAKE_WRITE_DELAY_US")) usleep((useconds_t)atol(getenv("FAKE_WRITE_DELAY_US")));
    if (getenv("FAKE_SINK")) return size;
    if (getenv("FAKE_ECHO_DELAY_MS"))
    {
        if (!echo_len)
        {
            clock_gettime(CLOCK_MONOTONIC, &echo_ready);
            long ms = atol(getenv("FAKE_ECHO_DELAY_MS"));
            echo_ready.tv_sec += ms / 1000;
            echo_ready.tv_nsec += (ms % 1000) * 1000000;
            if (echo_ready.tv_nsec >= 1000000000) { echo_ready.tv_sec++; echo_ready.tv_nsec -= 1000000000; }
        }
        memcpy(echo_buf + echo_len, buf, (size_t)size);
        echo_len += (size_t)size;
        return size;
    }

    size_t i = 0;
    while (up_need && i < (size_t)size) /* upload data bypasses command parsing */
    {
        up_buf[up_len++] = buf[i++];
        if (--up_need) continue;
        if (up_size == (size_t)-1)
        {
            up_size = ((size_t)up_buf[0] << 24) | ((size_t)up_buf[1] << 16) | ((size_t)up_buf[2] << 8) | up_buf[3];
            up_len = 0;
            up_need = up_size + 1;
            continue;
        }
        int ok = up_buf[up_size] == crc8(up_buf, up_size);
        FILE *out_file = ok ? fopen(up_path, "wb") : NULL;
        if (out_file) { fwrite(up_buf, 1, up_size, out_file); fclose(out_file); }
        out[out_len++] = (ok && out_file) ? 0 : 1;
    }
    memcpy(in + in_len, buf + i, (size_t)size - i);
    in_len += (size_t)size - i;
    while (in_len >= 7)
    {
        size_t plen = ((size_t)in[5] << 8) | in[6];
        if (in_len < 7 + plen) break;
        char arg[70000];
        memcpy(arg, in + 7, plen);
        arg[plen] = 0;
        int cmd = in[4];
        memmove(in, in + 7 + plen, in_len - 7 - plen);
        in_len -= 7 + plen;
        handle(cmd, arg);
    }
    return size;
}

int ftdi_read_data(struct ftdi_context *f, unsigned char *buf, int size)
{
    (void)f;
    if (getenv("FAKE_READ_FAIL")) return -1;
    deliver_echo();
    if (out_pos >= cut_at) return -1;
    size_t avail = out_len - out_pos;
    if (cut_at != (size_t)-1 && cut_at - out_pos < avail) avail = cut_at - out_pos;
    size_t n = avail < (size_t)size ? avail : (size_t)size;
    memcpy(buf, out + out_pos, n);
    out_pos += n;
    if (out_pos == out_len) out_pos = out_len = 0;
    if (n == 0) usleep(1000); /* like a real device, an empty read takes time */
    return (int)n;
}

int ftdi_init(struct ftdi_context *f) { (void)f; return 0; }
int ftdi_usb_open(struct ftdi_context *f, int v, int p) { (void)f; (void)v; (void)p; return 0; }
int ftdi_usb_open_desc(struct ftdi_context *f, int v, int p, const char *d, const char *s) { (void)f; (void)v; (void)p; (void)d; (void)s; return 0; }
int ftdi_usb_close(struct ftdi_context *f) { (void)f; return 0; }
void ftdi_deinit(struct ftdi_context *f) { (void)f; }
int ftdi_usb_reset(struct ftdi_context *f) { (void)f; return 0; }
/* Receive-buffer flushes drop what has arrived but not been read; transmit is never buffered here. */
int ftdi_tciflush(struct ftdi_context *f) { (void)f; deliver_echo(); out_len = out_pos = 0; return 0; }
int ftdi_tcoflush(struct ftdi_context *f) { (void)f; return 0; }
int ftdi_tcioflush(struct ftdi_context *f) { (void)f; echo_len = 0; out_len = out_pos = 0; return 0; }
int ftdi_set_bitmode(struct ftdi_context *f, unsigned char m, unsigned char b) { (void)f; (void)m; (void)b; return 0; }
int ftdi_read_data_set_chunksize(struct ftdi_context *f, unsigned int c) { (void)f; (void)c; return 0; }
int ftdi_write_data_set_chunksize(struct ftdi_context *f, unsigned int c) { (void)f; (void)c; return 0; }
const char *ftdi_get_error_string(struct ftdi_context *f) { (void)f; return "fakesat"; }
