/*
 * syscalls_stub.c
 *
 *  Created on: 2026/08/03
 *      Author: narus
 */


#include <sys/stat.h>
#include <string.h>
#include <tm/tmonitor.h>

int _close(int file) {
    (void)file;
    return -1;
}

int _lseek(int file, int ptr, int dir) {
    (void)file; (void)ptr; (void)dir;
    return 0;
}

int _read(int file, char *ptr, int len) {
    (void)file; (void)ptr; (void)len;
    return 0;
}

int _fstat(int file, struct stat *st) {
    (void)file;
    st->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int file) {
    (void)file;
    return 1;
}

int _write(int file, char *ptr, int len) {
    (void)file;
    char buf[128];
    int n = (len < 127) ? len : 127;
    memcpy(buf, ptr, n);
    buf[n] = '\0';
    tm_printf((UB*)"%s", buf);
    return len;
}
