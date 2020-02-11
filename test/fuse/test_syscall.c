// taken from https://github.com/libfuse/libfuse/blob/master/test/test_syscalls.c
// add additional test before main

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
// #include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <utime.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>

#ifndef ALLPERMS
# define ALLPERMS (S_ISUID|S_ISGID|S_ISVTX|S_IRWXU|S_IRWXG|S_IRWXO)/* 07777 */
#endif


static char testfile[1024];
static char testfile2[1024];
static char testdir[1024];
static char testdir2[1024];
static char testsock[1024];
static char subfile[1280];

static char testfile_r[1024];
static char testfile2_r[1024];
static char testdir_r[1024];
static char testdir2_r[1024];
static char subfile_r[1280];

static char testname[256];
static char testdata[] = "abcdefghijklmnopqrstuvwxyz";
static char testdata2[] = "1234567890-=qwertyuiop[]\asdfghjkl;'zxcvbnm,./";
static const char *testdir_files[] = { "f1", "f2", NULL};
static long seekdir_offsets[4];
static char zerodata[4096];
static int testdatalen = sizeof(testdata) - 1;
static int testdata2len = sizeof(testdata2) - 1;
static unsigned int testnum = 1;
static unsigned int select_test = 0;
static unsigned int skip_test = 0;

#define MAX_ENTRIES 1024

static void test_perror(const char *func, const char *msg)
{
    fprintf(stderr, "%s %s() - %s: %s\n", testname, func, msg,
        strerror(errno));
}

static void test_error(const char *func, const char *msg, ...)
    __attribute__ ((format (printf, 2, 3)));

static void __start_test(const char *fmt, ...)
    __attribute__ ((format (printf, 1, 2)));

static void test_error(const char *func, const char *msg, ...)
{
    va_list ap;
    fprintf(stderr, "%s %s() - ", testname, func);
    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static int is_dot_or_dotdot(const char *name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

static void success(void)
{
    fprintf(stderr, "%s OK\n", testname);
}

static void __start_test(const char *fmt, ...)
{
    unsigned int n;
    va_list ap;
    n = sprintf(testname, "%3i [", testnum++);
    va_start(ap, fmt);
    n += vsprintf(testname + n, fmt, ap);
    va_end(ap);
    sprintf(testname + n, "]");
}

#define start_test(msg, args...) { \
    if ((select_test && testnum != select_test) || \
        (testnum == skip_test)) { \
        testnum++; \
        return 0; \
    } \
    __start_test(msg, ##args);      \
}

#define PERROR(msg) test_perror(__FUNCTION__, msg)
#define ERROR(msg, args...) test_error(__FUNCTION__, msg, ##args)

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static int check_size(const char *path, int len)
{
    struct stat stbuf;
    int res = stat(path, &stbuf);
    if (res == -1) {
        PERROR("stat");
        return -1;
    }
    if (stbuf.st_size != len) {
        ERROR("length %u instead of %u", (int) stbuf.st_size,
              (int) len);
        return -1;
    }
    return 0;
}

static int check_type(const char *path, mode_t type)
{
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == -1) {
        PERROR("lstat");
        return -1;
    }
    if ((stbuf.st_mode & S_IFMT) != type) {
        ERROR("type 0%o instead of 0%o", stbuf.st_mode & S_IFMT, type);
        return -1;
    }
    return 0;
}

static int check_mode(const char *path, mode_t mode)
{
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == -1) {
        PERROR("lstat");
        return -1;
    }
    if ((stbuf.st_mode & ALLPERMS) != mode) {
        ERROR("mode 0%o instead of 0%o", stbuf.st_mode & ALLPERMS,
              mode);
        return -1;
    }
    return 0;
}

static int check_nlink(const char *path, nlink_t nlink)
{
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == -1) {
        PERROR("lstat");
        return -1;
    }
    if (stbuf.st_nlink != nlink) {
        ERROR("nlink %li instead of %li", (long) stbuf.st_nlink,
              (long) nlink);
        return -1;
    }
    return 0;
}

// helper function to check if file exists
static int check_exist(const char *path) {
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == 0) {  // file exists
        return 0;
    } else {
        ERROR("file not exist");
        return -1;
    }
}

static int check_nonexist(const char *path)
{
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == 0) {
        ERROR("file should not exist");
        return -1;
    }
    if (errno != ENOENT) {
        ERROR("file should not exist: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int check_buffer(const char *buf, const char *data, unsigned len)
{
    if (memcmp(buf, data, len) != 0) {
        ERROR("data mismatch");
        return -1;
    }
    return 0;
}

static int check_data(const char *path, const char *data, int offset,
              unsigned len)
{
    char buf[4096];
    int res;
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }
    if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
        PERROR("lseek");
        close(fd);
        return -1;
    }
    while (len) {
        int rdlen = len < sizeof(buf) ? len : sizeof(buf);
        res = read(fd, buf, rdlen);
        if (res == -1) {
            PERROR("read");
            close(fd);
            return -1;
        }
        if (res != rdlen) {
            ERROR("short read: %u instead of %u", res, rdlen);
            close(fd);
            return -1;
        }
        if (check_buffer(buf, data, rdlen) != 0) {
            close(fd);
            return -1;
        }
        data += rdlen;
        len -= rdlen;
    }
    res = close(fd);
    if (res == -1) {
        PERROR("close");
        return -1;
    }
    return 0;
}

static int check_dir_contents(const char *path, const char **contents)
{
    int i;
    int res;
    int err = 0;
    int found[MAX_ENTRIES];
    const char *cont[MAX_ENTRIES];
    DIR *dp;

    for (i = 0; contents[i]; i++) {
        assert(i < MAX_ENTRIES - 3);
        found[i] = 0;
        cont[i] = contents[i];
    }
    cont[i] = NULL;

    dp = opendir(path);
    if (dp == NULL) {
        PERROR("opendir");
        return -1;
    }
    memset(found, 0, sizeof(found));
    while(1) {
        struct dirent *de;
        errno = 0;
        de = readdir(dp);
        if (de == NULL) {
            if (errno) {
                PERROR("readdir");
                closedir(dp);
                return -1;
            }
            break;
        }
        if (is_dot_or_dotdot(de->d_name))
            continue;
        for (i = 0; cont[i] != NULL; i++) {
            assert(i < MAX_ENTRIES);
            if (strcmp(cont[i], de->d_name) == 0) {
                if (found[i]) {
                    ERROR("duplicate entry <%s>",
                          de->d_name);
                    err--;
                } else
                    found[i] = 1;
                break;
            }
        }
        if (!cont[i]) {
            ERROR("unexpected entry <%s>", de->d_name);
            err --;
        }
    }
    for (i = 0; cont[i] != NULL; i++) {
        if (!found[i]) {
            ERROR("missing entry <%s>", cont[i]);
            err--;
        }
    }
    res = closedir(dp);
    if (res == -1) {
        PERROR("closedir");
        return -1;
    }
    if (err)
        return -1;

    return 0;
}

static int create_file(const char *path, const char *data, int len)
{
    int res;
    int fd;

    unlink(path);
    fd = creat(path, 0644);
    if (fd == -1) {
        PERROR("creat");
        return -1;
    }
    if (len) {
        res = write(fd, data, len);
        if (res == -1) {
            PERROR("write");
            close(fd);
            return -1;
        }
        if (res != len) {
            ERROR("write is short: %u instead of %u", res, len);
            close(fd);
            return -1;
        }
    }
    res = close(fd);
    if (res == -1) {
        PERROR("close");
        return -1;
    }
    res = check_type(path, S_IFREG);
    if (res == -1)
        return -1;
    res = check_mode(path, 0644);
    if (res == -1)
        return -1;
    res = check_nlink(path, 1);
    if (res == -1)
        return -1;
    res = check_size(path, len);
    if (res == -1)
        return -1;

    if (len) {
        res = check_data(path, data, 0, len);
        if (res == -1)
            return -1;
    }

    return 0;
}

static int cleanup_dir(const char *path, const char **dir_files, int quiet)
{
    int i;
    int err = 0;

    for (i = 0; dir_files[i]; i++) {
        int res;
        char fpath[1280];
        sprintf(fpath, "%s/%s", path, dir_files[i]);
        res = unlink(fpath);
        if (res == -1 && !quiet) {
            PERROR("unlink");
            err --;
        }
    }
    if (err)
        return -1;

    return 0;
}

static int test_truncate(int len)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int res;

    start_test("truncate(%u)", (int) len);
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    res = truncate(testfile, len);
    if (res == -1) {
        PERROR("truncate");
        return -1;
    }
    res = check_size(testfile, len);
    if (res == -1)
        return -1;
    if (len > 0) {
        if (len <= datalen) {
            res = check_data(testfile, data, 0, len);
            if (res == -1)
                return -1;
        } else {
            res = check_data(testfile, data, 0, datalen);
            if (res == -1)
                return -1;
            res = check_data(testfile, zerodata, datalen,
                     len - datalen);
            if (res == -1)
                return -1;
        }
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    success();
    return 0;
}
/*
static int test_ftruncate(int len, int mode)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int res;
    int fd;

    start_test("ftruncate(%u) mode: 0%03o", len, mode);
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    fd = open(testfile, O_WRONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    res = fchmod(fd, mode);
    if (res == -1) {
        PERROR("fchmod");
        close(fd);
        return -1;
    }
    res = check_mode(testfile, mode);
    if (res == -1) {
        close(fd);
        return -1;
    }
    res = ftruncate(fd, len);
    if (res == -1) {
        PERROR("ftruncate");
        close(fd);
        return -1;
    }
    close(fd);
    res = check_size(testfile, len);
    if (res == -1)
        return -1;

    if (len > 0) {
        if (len <= datalen) {
            res = check_data(testfile, data, 0, len);
            if (res == -1)
                return -1;
        } else {
            res = check_data(testfile, data, 0, datalen);
            if (res == -1)
                return -1;
            res = check_data(testfile, zerodata, datalen,
                     len - datalen);
            if (res == -1)
                return -1;
        }
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;

    success();
    return 0;
}

static int test_seekdir(void)
{
    int i;
    int res;
    DIR *dp;
    struct dirent *de;

    start_test("seekdir");
    res = create_dir(testdir, testdir_files);
    if (res == -1)
        return res;

    dp = opendir(testdir);
    if (dp == NULL) {
        PERROR("opendir");
        return -1;
    }

    // Remember dir offsets
    for (i = 0; i < ARRAY_SIZE(seekdir_offsets); i++) {
        seekdir_offsets[i] = telldir(dp);
        errno = 0;
        de = readdir(dp);
        if (de == NULL) {
            if (errno) {
                PERROR("readdir");
                goto fail;
            }
            break;
        }
    }

    // Walk until the end of directory
    while (de)
        de = readdir(dp);

    // Start from the last valid dir offset and seek backwards
    for (i--; i >= 0; i--) {
        seekdir(dp, seekdir_offsets[i]);
        de = readdir(dp);
        if (de == NULL) {
            ERROR("Unexpected end of directory after seekdir()");
            goto fail;
        }
    }

    closedir(dp);
    res = cleanup_dir(testdir, testdir_files, 0);
    if (!res)
        success();
    return res;
fail:
    closedir(dp);
    cleanup_dir(testdir, testdir_files, 1);
    return -1;
}
#ifdef HAVE_COPY_FILE_RANGE
static int test_copy_file_range(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;
    int fd_in, fd_out;
    off_t pos_in = 0, pos_out = 0;

    start_test("copy_file_range");
    unlink(testfile);
    fd_in = open(testfile, O_CREAT | O_RDWR, 0644);
    if (fd_in == -1) {
        PERROR("creat");
        return -1;
    }
    res = write(fd_in, data, datalen);
    if (res == -1) {
        PERROR("write");
        close(fd_in);
        return -1;
    }
    if (res != datalen) {
        ERROR("write is short: %u instead of %u", res, datalen);
        close(fd_in);
        return -1;
    }

    unlink(testfile2);
    fd_out = creat(testfile2, 0644);
    if (fd_out == -1) {
        PERROR("creat");
        close(fd_in);
        return -1;
    }
    res = copy_file_range(fd_in, &pos_in, fd_out, &pos_out, datalen, 0);
    if (res == -1) {
        PERROR("copy_file_range");
        close(fd_in);
        close(fd_out);
        return -1;
    }
    if (res != datalen) {
        ERROR("copy is short: %u instead of %u", res, datalen);
        close(fd_in);
        close(fd_out);
        return -1;
    }

    res = close(fd_in);
    if (res == -1) {
        PERROR("close");
        return -1;
    }
    res = close(fd_out);
    if (res == -1) {
        PERROR("close");
        return -1;
    }

    err = check_data(testfile2, data, 0, datalen);

    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    res = unlink(testfile2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile2);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}
#else
static int test_copy_file_range(void)
{
    return 0;
}
#endif

static int test_utime(void)
{
    struct utimbuf utm;
    time_t atime = 987631200;
    time_t mtime = 123116400;
    int res;

    start_test("utime");
    res = create_file(testfile, NULL, 0);
    if (res == -1)
        return -1;

    utm.actime = atime;
    utm.modtime = mtime;
    res = utime(testfile, &utm);
    if (res == -1) {
        PERROR("utime");
        return -1;
    }
    res = check_times(testfile, atime, mtime);
    if (res == -1) {
        return -1;
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;

    success();
    return 0;
}

static int test_create(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;
    int fd;

    start_test("create");
    unlink(testfile);
    fd = creat(testfile, 0644);
    if (fd == -1) {
        PERROR("creat");
        return -1;
    }
    res = write(fd, data, datalen);
    if (res == -1) {
        PERROR("write");
        close(fd);
        return -1;
    }
    if (res != datalen) {
        ERROR("write is short: %u instead of %u", res, datalen);
        close(fd);
        return -1;
    }
    res = close(fd);
    if (res == -1) {
        PERROR("close");
        return -1;
    }
    res = check_type(testfile, S_IFREG);
    if (res == -1)
        return -1;
    err += check_mode(testfile, 0644);
    err += check_nlink(testfile, 1);
    err += check_size(testfile, datalen);
    err += check_data(testfile, data, 0, datalen);
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_create_unlink(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;
    int fd;

    start_test("create+unlink");
    unlink(testfile);
    fd = open(testfile, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd == -1) {
        PERROR("creat");
        return -1;
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        close(fd);
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    res = write(fd, data, datalen);
    if (res == -1) {
        PERROR("write");
        close(fd);
        return -1;
    }
    if (res != datalen) {
        ERROR("write is short: %u instead of %u", res, datalen);
        close(fd);
        return -1;
    }
    err += fcheck_type(fd, S_IFREG);
    err += fcheck_mode(fd, 0644);
    err += fcheck_nlink(fd, 0);
    err += fcheck_size(fd, datalen);
    err += fcheck_data(fd, data, 0, datalen);
    res = close(fd);
    if (res == -1) {
        PERROR("close");
        err--;
    }
    if (err)
        return -1;

    success();
    return 0;
}
*/
#ifndef __FreeBSD__
static int test_mknod(void)
{
    int err = 0;
    int res;

    start_test("mknod");
    unlink(testfile);
    res = mknod(testfile, 0644, 0);
    if (res == -1) {
        PERROR("mknod");
        return -1;
    }
    res = check_type(testfile, S_IFREG);
    if (res == -1)
        return -1;
    err += check_mode(testfile, 0644);
    err += check_nlink(testfile, 1);
    err += check_size(testfile, 0);
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}
#endif

#define test_open(exist, flags, mode)  do_test_open(exist, flags, #flags, mode)

static int do_test_open(int exist, int flags, const char *flags_str, int mode)
{
    char buf[4096];
    const char *data = testdata;
    int datalen = testdatalen;
    unsigned currlen = 0;
    int err = 0;
    int res;
    int fd;
    off_t off;

    start_test("open(%s, %s, 0%03o)", exist ? "+" : "-", flags_str, mode);
    unlink(testfile);
    if (exist) {
        res = create_file(testfile_r, testdata2, testdata2len);
        if (res == -1)
            return -1;

        currlen = testdata2len;
    }

    fd = open(testfile, flags, mode);
    if ((flags & O_CREAT) && (flags & O_EXCL) && exist) {
        if (fd != -1) {
            ERROR("open should have failed");
            close(fd);
            return -1;
        } else if (errno == EEXIST)
            goto succ;
    }
    if (!(flags & O_CREAT) && !exist) {
        if (fd != -1) {
            ERROR("open should have failed");
            close(fd);
            return -1;
        } else if (errno == ENOENT)
            goto succ;
    }
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    if (flags & O_TRUNC)
        currlen = 0;

    err += check_type(testfile, S_IFREG);
    if (exist)
        err += check_mode(testfile, 0644);
    else
        err += check_mode(testfile, mode);
    err += check_nlink(testfile, 1);
    err += check_size(testfile, currlen);
    if (exist && !(flags & O_TRUNC) && (mode & S_IRUSR))
        err += check_data(testfile, testdata2, 0, testdata2len);

    res = write(fd, data, datalen);
    if ((flags & O_ACCMODE) != O_RDONLY) {
        if (res == -1) {
            PERROR("write");
            err --;
        } else if (res != datalen) {
            ERROR("write is short: %u instead of %u", res, datalen);
            err --;
        } else {
            if (datalen > (int) currlen)
                currlen = datalen;

            err += check_size(testfile, currlen);

            if (mode & S_IRUSR) {
                err += check_data(testfile, data, 0, datalen);
                if (exist && !(flags & O_TRUNC) &&
                    testdata2len > datalen)
                    err += check_data(testfile,
                              testdata2 + datalen,
                              datalen,
                              testdata2len - datalen);
            }
        }
    } else {
        if (res != -1) {
            ERROR("write should have failed");
            err --;
        } else if (errno != EBADF) {
            PERROR("write");
            err --;
        }
    }
    off = lseek(fd, SEEK_SET, 0);
    if (off == (off_t) -1) {
        PERROR("lseek");
        err--;
    } else if (off != 0) {
        ERROR("offset should have returned 0");
        err --;
    }
    res = read(fd, buf, sizeof(buf));
    if ((flags & O_ACCMODE) != O_WRONLY) {
        if (res == -1) {
            PERROR("read");
            err--;
        } else {
            int readsize =
                currlen < sizeof(buf) ? currlen : sizeof(buf);
            if (res != readsize) {
                ERROR("read is short: %i instead of %u",
                      res, readsize);
                err--;
            } else {
                if ((flags & O_ACCMODE) != O_RDONLY) {
                    err += check_buffer(buf, data, datalen);
                    if (exist && !(flags & O_TRUNC) &&
                        testdata2len > datalen)
                        err += check_buffer(buf + datalen,
                                    testdata2 + datalen,
                                    testdata2len - datalen);
                } else if (exist)
                    err += check_buffer(buf, testdata2,
                                testdata2len);
            }
        }
    } else {
        if (res != -1) {
            ERROR("read should have failed");
            err --;
        } else if (errno != EBADF) {
            PERROR("read");
            err --;
        }
    }

    res = close(fd);
    if (res == -1) {
        PERROR("close");
        return -1;
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    res = check_nonexist(testfile_r);
    if (res == -1)
        return -1;
    if (err)
        return -1;

succ:
    success();
    return 0;
}
/*
#define test_open_acc(flags, mode, err)  \
    do_test_open_acc(flags, #flags, mode, err)

static int do_test_open_acc(int flags, const char *flags_str, int mode, int err)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int res;
    int fd;

    start_test("open_acc(%s) mode: 0%03o message: '%s'", flags_str, mode,
           strerror(err));
    unlink(testfile);
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    res = chmod(testfile, mode);
    if (res == -1) {
        PERROR("chmod");
        return -1;
    }

    res = check_mode(testfile, mode);
    if (res == -1)
        return -1;

    fd = open(testfile, flags);
    if (fd == -1) {
        if (err != errno) {
            PERROR("open");
            return -1;
        }
    } else {
        if (err) {
            ERROR("open should have failed");
            close(fd);
            return -1;
        }
        close(fd);
    }
    success();
    return 0;
}

static int test_symlink(void)
{
    char buf[1024];
    const char *data = testdata;
    int datalen = testdatalen;
    int linklen = strlen(testfile);
    int err = 0;
    int res;

    start_test("symlink");
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    unlink(testfile2);
    res = symlink(testfile, testfile2);
    if (res == -1) {
        PERROR("symlink");
        return -1;
    }
    res = check_type(testfile2, S_IFLNK);
    if (res == -1)
        return -1;
    err += check_mode(testfile2, 0777);
    err += check_nlink(testfile2, 1);
    res = readlink(testfile2, buf, sizeof(buf));
    if (res == -1) {
        PERROR("readlink");
        err--;
    }
    if (res != linklen) {
        ERROR("short readlink: %u instead of %u", res, linklen);
        err--;
    }
    if (memcmp(buf, testfile, linklen) != 0) {
        ERROR("link mismatch");
        err--;
    }
    err += check_size(testfile2, datalen);
    err += check_data(testfile2, data, 0, datalen);
    res = unlink(testfile2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile2);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_link(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;

    start_test("link");
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    unlink(testfile2);
    res = link(testfile, testfile2);
    if (res == -1) {
        PERROR("link");
        return -1;
    }
    res = check_type(testfile2, S_IFREG);
    if (res == -1)
        return -1;
    err += check_mode(testfile2, 0644);
    err += check_nlink(testfile2, 2);
    err += check_size(testfile2, datalen);
    err += check_data(testfile2, data, 0, datalen);
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;

    err += check_nlink(testfile2, 1);
    res = unlink(testfile2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile2);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_link2(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;

    start_test("link-unlink-link");
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    unlink(testfile2);
    res = link(testfile, testfile2);
    if (res == -1) {
        PERROR("link");
        return -1;
    }
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    res = link(testfile2, testfile);
    if (res == -1) {
        PERROR("link");
    }
    res = check_type(testfile, S_IFREG);
    if (res == -1)
        return -1;
    err += check_mode(testfile, 0644);
    err += check_nlink(testfile, 2);
    err += check_size(testfile, datalen);
    err += check_data(testfile, data, 0, datalen);

    res = unlink(testfile2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    err += check_nlink(testfile, 1);
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_rename_file(void)
{
    const char *data = testdata;
    int datalen = testdatalen;
    int err = 0;
    int res;

    start_test("rename file");
    res = create_file(testfile, data, datalen);
    if (res == -1)
        return -1;

    unlink(testfile2);
    res = rename(testfile, testfile2);
    if (res == -1) {
        PERROR("rename");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    res = check_type(testfile2, S_IFREG);
    if (res == -1)
        return -1;
    err += check_mode(testfile2, 0644);
    err += check_nlink(testfile2, 1);
    err += check_size(testfile2, datalen);
    err += check_data(testfile2, data, 0, datalen);
    res = unlink(testfile2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile2);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_rename_dir(void)
{
    int err = 0;
    int res;

    start_test("rename dir");
    res = create_dir(testdir, testdir_files);
    if (res == -1)
        return -1;

    rmdir(testdir2);
    res = rename(testdir, testdir2);
    if (res == -1) {
        PERROR("rename");
        cleanup_dir(testdir, testdir_files, 1);
        return -1;
    }
    res = check_nonexist(testdir);
    if (res == -1) {
        cleanup_dir(testdir, testdir_files, 1);
        return -1;
    }
    res = check_type(testdir2, S_IFDIR);
    if (res == -1) {
        cleanup_dir(testdir2, testdir_files, 1);
        return -1;
    }
    err += check_mode(testdir2, 0755);
    err += check_dir_contents(testdir2, testdir_files);
    err += cleanup_dir(testdir2, testdir_files, 0);
    res = rmdir(testdir2);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir2);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

static int test_rename_dir_loop(void)
{
#define PATH(p)     (snprintf(path, sizeof path, "%s/%s", testdir, p), path)
#define PATH2(p)    (snprintf(path2, sizeof path2, "%s/%s", testdir, p), path2)

    char path[1280], path2[1280];
    int err = 0;
    int res;

    start_test("rename dir loop");

    res = create_dir(testdir, testdir_files);
    if (res == -1)
        return -1;

    res = mkdir(PATH("a"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    res = rename(PATH("a"), PATH2("a"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    errno = 0;
    res = rename(PATH("a"), PATH2("a/b"));
    if (res == 0 || errno != EINVAL) {
        PERROR("rename");
        goto fail;
    }

    res = mkdir(PATH("a/b"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    res = mkdir(PATH("a/b/c"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    errno = 0;
    res = rename(PATH("a"), PATH2("a/b/c"));
    if (res == 0 || errno != EINVAL) {
        PERROR("rename");
        goto fail;
    }

    errno = 0;
    res = rename(PATH("a"), PATH2("a/b/c/a"));
    if (res == 0 || errno != EINVAL) {
        PERROR("rename");
        goto fail;
    }

    errno = 0;
    res = rename(PATH("a/b/c"), PATH2("a"));
    if (res == 0 || errno != ENOTEMPTY) {
        PERROR("rename");
        goto fail;
    }

    res = open(PATH("a/foo"), O_CREAT, 0644);
    if (res == -1) {
        PERROR("open");
        goto fail;
    }
    close(res);

    res = rename(PATH("a/foo"), PATH2("a/bar"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/bar"), PATH2("a/foo"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/foo"), PATH2("a/b/bar"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/b/bar"), PATH2("a/foo"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/foo"), PATH2("a/b/c/bar"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/b/c/bar"), PATH2("a/foo"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = open(PATH("a/bar"), O_CREAT, 0644);
    if (res == -1) {
        PERROR("open");
        goto fail;
    }
    close(res);

    res = rename(PATH("a/foo"), PATH2("a/bar"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    unlink(PATH("a/bar"));

    res = rename(PATH("a/b"), PATH2("a/d"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/d"), PATH2("a/b"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = mkdir(PATH("a/d"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    res = rename(PATH("a/b"), PATH2("a/d"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = rename(PATH("a/d"), PATH2("a/b"));
    if (res == -1) {
        PERROR("rename");
        goto fail;
    }

    res = mkdir(PATH("a/d"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    res = mkdir(PATH("a/d/e"), 0755);
    if (res == -1) {
        PERROR("mkdir");
        goto fail;
    }

    errno = 0;
    res = rename(PATH("a/b"), PATH2("a/d"));
    if (res == 0 || errno != ENOTEMPTY) {
        PERROR("rename");
        goto fail;
    }

    rmdir(PATH("a/d/e"));
    rmdir(PATH("a/d"));

    rmdir(PATH("a/b/c"));
    rmdir(PATH("a/b"));
    rmdir(PATH("a"));

    err += cleanup_dir(testdir, testdir_files, 0);
    res = rmdir(testdir);
    if (res == -1) {
        PERROR("rmdir");
        goto fail;
    }
    res = check_nonexist(testdir);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;

fail:
    unlink(PATH("a/bar"));

    rmdir(PATH("a/d/e"));
    rmdir(PATH("a/d"));
 
    rmdir(PATH("a/b/c"));
    rmdir(PATH("a/b"));
    rmdir(PATH("a"));

    cleanup_dir(testdir, testdir_files, 1);
    rmdir(testdir);

    return -1;

#undef PATH2
#undef PATH
}

#ifndef __FreeBSD__
static int test_mkfifo(void)
{
    int res;
    int err = 0;

    start_test("mkfifo");
    unlink(testfile);
    res = mkfifo(testfile, 0644);
    if (res == -1) {
        PERROR("mkfifo");
        return -1;
    }
    res = check_type(testfile, S_IFIFO);
    if (res == -1)
        return -1;
    err += check_mode(testfile, 0644);
    err += check_nlink(testfile, 1);
    res = unlink(testfile);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testfile);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}
#endif
*/
static int test_mkdir(void)
{
    int res;
    int err = 0;
    const char *dir_contents[] = {NULL};

    start_test("mkdir");
    rmdir(testdir);
    res = mkdir(testdir, 0755);
    if (res == -1) {
        PERROR("mkdir");
        return -1;
    }
    res = check_type(testdir, S_IFDIR);
    if (res == -1)
        return -1;
    err += check_mode(testdir, 0755);
    /* Some file systems (like btrfs) don't track link
       count for directories */
    //err += check_nlink(testdir, 2);
    err += check_dir_contents(testdir, dir_contents);
    res = rmdir(testdir);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

/*
static int test_socket(void)
{
    struct sockaddr_un su;
    int fd;
    int res;
    int err = 0;

    start_test("socket");
    if (strlen(testsock) + 1 > sizeof(su.sun_path)) {
        fprintf(stderr, "Need to shorten mount point by %lu chars\n",
            strlen(testsock) + 1 - sizeof(su.sun_path));
        return -1;
    }
    unlink(testsock);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        PERROR("socket");
        return -1;
    }
    su.sun_family = AF_UNIX;
    strncpy(su.sun_path, testsock, sizeof(su.sun_path) - 1);
    su.sun_path[sizeof(su.sun_path) - 1] = '\0';
    res = bind(fd, (struct sockaddr*)&su, sizeof(su));
    if (res == -1) {
        PERROR("bind");
        return -1;
    }

    res = check_type(testsock, S_IFSOCK);
    if (res == -1)
        return -1;
    err += check_nlink(testsock, 1);
    close(fd);
    res = unlink(testsock);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(testsock);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}

#define test_create_ro_dir(flags)    \
    do_test_create_ro_dir(flags, #flags)

static int do_test_create_ro_dir(int flags, const char *flags_str)
{
    int res;
    int err = 0;
    int fd;

    start_test("open(%s) in read-only directory", flags_str);
    rmdir(testdir);
    res = mkdir(testdir, 0555);
    if (res == -1) {
        PERROR("mkdir");
        return -1;
    }
    fd = open(subfile, flags, 0644);
    if (fd != -1) {
        close(fd);
        unlink(subfile);
        ERROR("open should have failed");
        err--;
    } else {
        res = check_nonexist(subfile);
        if (res == -1)
            err--;
    }
    unlink(subfile);
    res = rmdir(testdir);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir);
    if (res == -1)
        return -1;
    if (err)
        return -1;

    success();
    return 0;
}
*/

// helper function to check if file exists
static int check_exist(const char *path) {
    struct stat stbuf;
    int res = lstat(path, &stbuf);
    if (res == 0) {  // file exists
        return 0;
    } else {
        ERROR("file not exist");
        return -1;
    }
}

// additional read test
static int test_read_add(void) {
    const char *data = testdata;
    int datalen = testdatalen;
    int res;
    int fd;

    // set up read buffer and read length
    char buf[2*sizeof(testdata)];
    int readlen;
    int offset;

    start_test("read additional");

    // read from 0 to readlen <= datalen
    res = create_file(testfile, data, datalen);
    if (res == -1) {
        PERROR("creat");
        return -1;
    }

    // printf("access %s with return = %d\n", testfile, access(testfile, F_OK));
    fd = open(testfile, O_RDONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    readlen = datalen / 2;
    res = read(fd, buf, readlen);
    if (res == -1) {
        ERROR("case 1: read %d bytes from fd %d, buf = %s", res, fd, buf);
        close(fd);
        return -1;
    }
    if (res != readlen) {
        ERROR("incorrect readlen: %u instead of %u", res, readlen);
        close(fd);
        return -1;
    }
    if (strncmp(buf, data, readlen) != 0) {
        ERROR("incorrect read: %s instead of %s", buf, data);
        close(fd);
        return -1;
    }
    close(fd);

    // read from 0 to len > datalen
    res = create_file(testfile, data, datalen);
    if (res == -1) {
        PERROR("creat");
        return -1;
    }

    fd = open(testfile, O_RDONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    readlen = datalen + datalen / 2;
    res = read(fd, buf, readlen);
    if (res == -1) {
        ERROR("case 2: read %u bytes from fd %d", readlen, fd);
        close(fd);
        return -1;
    }  
    if (res != datalen) {
        ERROR("incorrect readlen: %u instead of %u", res, datalen);
        close(fd);
        return -1;
    }
    if (strncmp(buf, data, datalen) != 0) {
        ERROR("incorrect read: %s instead of %s", buf, data);
        close(fd); 
        return -1;
    }

    // read from offset <= datalen
    // with offset + readlen <= datalen
    res = create_file(testfile, data, datalen);
    if (res == -1) {
        PERROR("creat");
        return -1;
    }

    fd = open(testfile, O_RDONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    readlen = datalen / 2;
    offset = datalen - readlen;
    res = lseek(fd, offset, SEEK_SET);
    if (res == offset - 1) { 
        PERROR("lseek");
        close(fd);
        return -1;
    }
    if (res != offset) {
        ERROR("offset should have returned %u", offset);
        close(fd);
        return -1;
    }

    res = read(fd, buf, readlen);
    if (res == -1) {
        ERROR("case 3: read %u bytes from fd %d", readlen, fd);
        close(fd);
        return -1;
    }
    if (res != readlen) {
        ERROR("incorrect readlen: %u instead of %u", res, readlen);
        close(fd);
        return -1;
    }
    if (strncmp(buf, data + offset, readlen) != 0) {
        ERROR("incorrect read: %s instread of %s", buf, data + offset);
        close(fd);
        return -1;
    }
    close(fd);

    // read from offset <= datalen
    // with offset + readlen > datalen
    res = create_file(testfile, data, datalen);
    if (res == -1) {
        PERROR("creat");
        return -1;
    }

    fd = open(testfile, O_RDONLY);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }
    
    readlen = datalen;
    offset = datalen / 2;
    
    res = lseek(fd, offset, SEEK_SET);
    if (res == offset - 1) {
        PERROR("lseek");
        close(fd);
        return -1;
    }
    if (res != offset) {
        ERROR("offset should have returned %u", offset);
        close(fd);
        return -1;
    }

    res = read(fd, buf, readlen);
    if (res == -1) {
        ERROR("case 4: read %u bytes from fd %d", readlen, fd);
        close(fd);
        return -1;
    }
    if (res != datalen - offset) {
        ERROR("incorrect readlen: %u instead of %u", res, datalen-offset);
        close(fd);
        return -1;
    }
    if (strncmp(buf, data + offset, datalen-offset) != 0) {
        ERROR("incorrect read: %s instread of %s", buf, data + offset);
        close(fd);
        return -1;
    }
    close(fd);

    // read from offset > datalen;
    offset = datalen + datalen / 2;
    res = lseek(fd, offset, SEEK_SET);
    if (res != -1) {
        ERROR("offset > filesize: %d instead of %d", res, offset-1);
        close(fd);
        return -1;
    }
 
    // all test passed
    success();
    return 0;       
}


// additional write test
static int test_write_add() {
    const char *data = testdata;
    int datalen = testdatalen;
    int res;
    int fd;

    // set up read buffer and read length
    char buf[2*sizeof(testdata)];
    int writelen;
    int offset;

    start_test("write additional");

    // write new file
    res = create_file(testfile, data, datalen);
    if (res == -1) {
        PERROR("creat");
        return -1;
    }   
    res = check_data(testfile, data, 0, datalen);
    if (res == -1) {
        ERROR("read != write");
        return -1;
    }   

    // write existing file from start
    // write half of entire data
    writelen = datalen / 2;
    fd = open(testfile, O_RDWR);
    if (fd == -1) {
        PERROR("open");
        return -1;
    }

    res = lseek(fd, 0, SEEK_SET);
    if (res == -1) {
        PERROR("lseek");
        close(fd);
        return -1;
    }

    res = write(fd, data + datalen - writelen, writelen);
    if (res == -1) {
        PERROR("write");
        close(fd);
        return -1;
    }
    if (res != writelen) {
        ERROR("incorrect writelen: %u instead of %u", res, writelen);
    }
    res = check_data(testfile, data + datalen - writelen, 0, writelen);
    if (res == -1) {
        ERROR("read != write");
        close(fd);
        return -1;
    }

    // pass all tests
    success();
    return 0;
}

// additional seek test (in test_read_add)

// additional unlink test (in test_rmdir_add)

// additional mkdir test
static int test_mkdir_add(void) {
    char dirpath[64];
    int res;

    // construct dir path
    strcpy(dirpath, "");
    strcat(dirpath, testdir2);
    strcat(dirpath, "/testdir");

    start_test("mkdir additional")
 
    // make 1 sub dir
    rmdir(testdir);
    res = mkdir(testdir, 0755);
    if (res == -1) {
        PERROR("mkdir");
        return -1;
    } 
    res = check_type(testdir, S_IFDIR);
    if (res == -1) {
        ERROR("%s not type directory", testdir);
        return -1;
    }
    res = check_mode(testdir, 0755);
    if (res == -1) {
        ERROR("mode != 0755");
        return -1;
    }

    // make nested dir, should return error
    rmdir(testdir);
    res = check_nonexist(testdir);
    if (res == -1) {
        ERROR("%s should not exist", testdir);
        return -1;
    }

    res = mkdir(dirpath, 0755);
    if (res != -1) {
        ERROR("should not create nested dir %s at once", dirpath);
        return -1;
    }
    res = check_nonexist(dirpath);
    if (res == -1) {
        ERROR("%s should not exist", dirpath);
        return -1;
    }

    // make nest dir from parent
    res = mkdir(testdir2, 0755);
    if (res == -1) {
        PERROR("mkdir");
        return -1;
    }
    res = mkdir(dirpath, 0755);
    if (res == -1) {
        ERROR("cannot create nested dir %s", dirpath);   
        return -1;
    }
    res = check_type(dirpath, S_IFDIR);
    if (res == -1) {
        ERROR("%s not type directory", dirpath);
        return -1;
    }
    res = check_mode(dirpath, 0755);
    if (res == -1) {
        ERROR("mode != 0755");
        return -1;
    }

    // clean up folder
    rmdir(dirpath);
    res = check_nonexist(dirpath);
    if (res == -1) {
        ERROR("%s should not exist", dirpath);
        return -1;
    }     
    rmdir(testdir2);
    res = check_nonexist(dirpath);
    if (res == -1) {
        ERROR("%s should not exist", dirpath);
        return -1;
    }

    // pass all tests
    success();
    return 0;
}

// additional readdir test
static int test_readdir_add(void) {
    char parentdir[64];  // temp/testdir/
    char dirpath1[64];   // temp/testdir/testdir -- empty dir
    char dirpath2[64];   // temp/testdir/testdir2/ 
    char filepath1[64];  // temp/testdir/testfile -- content of parentdir
    char filepath2[64];  // temp/testdir/testdir2/testfile2
                         //  -- content of dirpath2
    const char *data1 = testdata;
    const char *data2 = testdata2;
    int datalen1 = testdatalen;
    int datalen2 = testdata2len;

    DIR *dir1;
    DIR *dir2;
    struct dirent *entry;
    int res;

    // construct path names
    strcpy(parentdir, testdir);
    
    strcpy(dirpath1, testdir);
    strcat(dirpath1, "/testdir");
    
    strcpy(dirpath2, testdir);
    strcat(dirpath2, "/testdir2");

    strcpy(filepath1, parentdir);
    strcat(filepath1, "/testfile");

    strcpy(filepath2, dirpath2);
    strcat(filepath2, "/testfile2");
    
    /*
    printf("parentdir: %s\n", parentdir);
    printf("dirpath1: %s\n", dirpath1);
    printf("dirpath2: %s\n", dirpath2);
    printf("filepath1: %s\n", filepath1);
    printf("filepath2: %s\n", filepath2);
    */

    // create dir and file
    unlink(filepath1);
    unlink(filepath2);
    rmdir(dirpath1);
    rmdir(dirpath2);
    rmdir(parentdir);
    res = check_nonexist(parentdir);
    if (res == -1) {
        ERROR("%s should not exist", parentdir);
        return -1;
    }
    res = check_nonexist(dirpath1);
    if (res == -1) {
        ERROR("%s should not exist", dirpath1);
        return -1;
    }
    res = check_nonexist(dirpath2);
    if (res == -1) {
        ERROR("%s should not exist", dirpath2);
        return -1;
    }
    res = check_nonexist(filepath1);
    if (res == -1) {
        ERROR("%s should not exist", filepath1);
        return -1;
    }
    res = check_nonexist(filepath2);
    if (res != 0) {
        ERROR("%s should not exist", filepath2);
        return -1;
    }

    res = mkdir(parentdir, 775) + mkdir(dirpath1, 775) + mkdir(dirpath2, 775)
        + create_file(filepath1, data1, datalen1) 
        + create_file(filepath2, data2, datalen2);
    if (res != 0) {
        ERROR("create dir and file failed");
        return -1;
    }
    res = check_exist(parentdir) + check_exist(dirpath1) 
        + check_exist(dirpath2) + check_data(filepath1, data1, 0, datalen1);
        + check_data(filepath2, data2, 0, datalen2);
    if (res != 0) {
        ERROR("created dir/file content not match");
        return -1;
    }

    start_test("readdir additional");

    // read parentdir
    if ((dir1 = opendir(parentdir)) == NULL) {
        PERROR("opendir");
        return -1;
    } else {
        int count = 0;
        while ((entry = readdir(dir1)) != NULL) {
            count += 1; 
        }
        closedir(dir1);
        if ((dir2 = opendir(dirpath2)) == NULL) {
            PERROR("opendir");
            return -1;
        } else {
            while ((entry = readdir(dir2)) != NULL) {
                count += 1;
            }
            closedir(dir2);
        }
    
        // testfile, testdir, testdir2, ., .., testdir2/testfile2
        // testdir2/., testdir2/..
        if (count != 8) {
            ERROR("incorrect file number: %d instead of 8", count);
            return -1;
        }
    }   
    
    // clean folder
    unlink(filepath1);
    unlink(filepath2);
    rmdir(dirpath1);
    rmdir(dirpath2);
    rmdir(parentdir);
    res = check_nonexist(parentdir);
    if (res == -1) {
        ERROR("here %s should not exist", parentdir);
        return -1;
    }
    res = check_nonexist(dirpath1);
    if (res == -1) {
        ERROR("%s should not exist", dirpath1);
        return -1;
    }
    res = check_nonexist(dirpath2);
    if (res == -1) {
        ERROR("%s should not exist", dirpath2);
        return -1;
    }
    res = check_nonexist(filepath1);
    if (res == -1) {
        ERROR("%s should not exist", filepath1);
        return -1;
    }
    res = check_nonexist(filepath2);
    if (res != 0) {
        ERROR("%s should not exist", filepath2);
        return -1;
    }

    success();
    return 0;
}

// additional rmdir test
static int test_rmdir_add(void) {
    char dirpath[64];
    char filepath[64];
    char filepath2[64];
    const char *data = testdata;
    int datalen = testdatalen;
    int res;

    // construct dirpath: testdir2/testdir
    strcpy(dirpath, "");
    strcat(dirpath, testdir2);
    strcat(dirpath, "/testdir");

    // construct filepath: testdir/testfile
    strcpy(filepath, "");
    strcat(filepath, testdir);
    strcat(filepath, "/testfile");

    // constrct filepath2: testdir2/testdir1/testfile
    strcpy(filepath2, "");
    strcat(filepath2, dirpath);
    strcat(filepath2, "/testfile");

    start_test("rmdir additional")   
    
    // remove empty dir
    unlink(filepath);
    rmdir(testdir);
    res = check_nonexist(testdir);
    if (res == -1) {
        ERROR("%s already exists", testdir);
        return -1;
    }
    res = mkdir(testdir, 0755);
    res = check_exist(testdir);
    if (res == -1) {
        ERROR("%s not created", testdir);
        return -1;
    }
    res = rmdir(testdir);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir);
    if (res == -1) {
        ERROR("%s should be removed", testdir);
        return -1;
    }
      
    // remove non empty dir with file
    res = mkdir(testdir, 775);
    if (res == -1) {
        PERROR("mkdir");
        return -1;
    }
    res = create_file(filepath, data, datalen);
    if (res == -1) {
        PERROR("create_file");
        return -1;
    }
    res = check_exist(filepath);
    if (res == -1) {
        ERROR("%s not existed", filepath);
        return -1;
    }

    // 1. remove dir directly should fail
    res = rmdir(testdir);
    if (res != -1) {
        ERROR("should not remove non empty dir %s", testdir);
        return -1;
    }
    res = check_exist(testdir);
    if (res == -1) {
        ERROR("%s not existed", testdir);
        return -1;
    }
    res = check_exist(filepath);
    if (res == -1) {
        ERROR("%s not existed", filepath);
        return -1;
    }
    res = check_data(filepath, data, 0, datalen);
    if (res == -1) {
        ERROR("content changed for file %s", filepath);
        return -1;
    } 

    // 2. remove dir by remove file first 
    res = unlink(filepath);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(filepath);
    if (res == -1) {
        ERROR("%s should be unlinked", filepath);
        return -1;
    }

    res = rmdir(testdir);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir);
    if (res == -1) {
        ERROR("%s should be removed", testdir);
        return -1;
    }

    // remove non empty dir with sub dir    
    unlink(filepath2);
    rmdir(dirpath);
    rmdir(testdir2);
    res = check_nonexist(testdir2);
    if (res == -1) {
        ERROR("%s already exists", testdir2);
        return -1;
    }
    res = mkdir(testdir2, 775);
    if (res != 0) {
        ERROR("failed to create dir %s", testdir2);
        return -1;
    }
    res =mkdir(dirpath, 775);
    if (res != 0) {
        ERROR("failed to create dir %s", dirpath);
        return -1;
    }
    res = create_file(filepath2, data, datalen);
    if (res == -1) {
        ERROR("failed to create file %s", filepath2);
        return -1;
    }
    
    // 1. remove dir directly should fail
    res = rmdir(testdir2);
    if (res != -1) {
        ERROR("should not remove non empty dir %s", testdir2);
        return -1;
    }
    res = check_exist(testdir2) 
        + check_exist(dirpath) 
        + check_exist(filepath2);
    if (res != 0) {
        ERROR("file not exist");
        return -1;
    } 
    
    // 2. remove file, rmdir testdir2 should fail
    res = unlink(filepath2);
    if (res == -1) {
        PERROR("unlink");
        return -1;
    }
    res = check_nonexist(filepath2);
    if (res == -1) {
        ERROR("file should be unlinked %s", filepath2);
        return -1;
    }

    res = rmdir(testdir2);
    if (res != -1) {
        ERROR("should not remove non empty dir %s", testdir2);
        return -1;
    }
    res = check_exist(testdir2) + check_exist(dirpath);
    if (res != 0) {
        ERROR("file not existed");
        return -1;
    }

    // 3. remove sub dir
    res = rmdir(dirpath);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(dirpath);
    if (res == -1) {
        ERROR("should be removed: %s", dirpath);
        return -1;
    }

    // remove parent dir
    res = check_exist(testdir2);
    if (res == -1) {
        ERROR("file not exist: %s", testdir2);
        return -1;
    }
    res = rmdir(testdir2);
    if (res == -1) {
        PERROR("rmdir");
        return -1;
    }
    res = check_nonexist(testdir2);
    if (res == -1) {
        ERROR("should be removed: %s", testdir2);
        return -1;
    }

    // pass all tests
    success();
    return 0;
}


int main(int argc, char *argv[])
{
    const char *basepath;
    const char *realpath;
    int err = 0;
    int a;
    int is_root;

    umask(0);
    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s testdir [:realdir] [[-]test#]\n", argv[0]);
        return 1;
    }
    basepath = argv[1];
    realpath = basepath;
    for (a = 2; a < argc; a++) {
        char *endptr;
        char *arg = argv[a];
        if (arg[0] == ':') {
            realpath = arg + 1;
        } else {
            if (arg[0] == '-') {
                arg++;
                skip_test = strtoul(arg, &endptr, 10);
            } else {
                select_test = strtoul(arg, &endptr, 10);
            }
            if (arg[0] == '\0' || *endptr != '\0') {
                fprintf(stderr, "invalid number: '%s'\n", arg);
                return 1;
            }
        }
    }
    assert(strlen(basepath) < 512);
    assert(strlen(realpath) < 512);
    if (basepath[0] != '/') {
        fprintf(stderr, "testdir must be an absolute path\n");
        return 1;
    }

    sprintf(testfile, "%s/testfile", basepath);
    sprintf(testfile2, "%s/testfile2", basepath);
    sprintf(testdir, "%s/testdir", basepath);
    sprintf(testdir2, "%s/testdir2", basepath);
    sprintf(subfile, "%s/subfile", testdir2);
    sprintf(testsock, "%s/testsock", basepath);

    sprintf(testfile_r, "%s/testfile", realpath);
    sprintf(testfile2_r, "%s/testfile2", realpath);
    sprintf(testdir_r, "%s/testdir", realpath);
    sprintf(testdir2_r, "%s/testdir2", realpath);
    sprintf(subfile_r, "%s/subfile", testdir2_r);

    is_root = (geteuid() == 0);

    //err += test_create();
    //err += test_create_unlink();
    //err += test_symlink();
    //err += test_link();
    //err += test_link2();
#ifndef __FreeBSD__ 
    err += test_mknod();
    //err += test_mkfifo();
#endif
    err += test_mkdir();
    //err += test_rename_file();
    //err += test_rename_dir();
    //err += test_rename_dir_loop();
    //err += test_seekdir();
    //err += test_socket();
    //err += test_utime();
       
    err += test_truncate(0);
    err += test_truncate(testdatalen / 2);
    err += test_truncate(testdatalen);
    err += test_truncate(testdatalen + 100);
    
    //err += test_ftruncate(0, 0600);
    //err += test_ftruncate(testdatalen / 2, 0600);
    //err += test_ftruncate(testdatalen, 0600);
    //err += test_ftruncate(testdatalen + 100, 0600);
    //err += test_ftruncate(0, 0400);
    //err += test_ftruncate(0, 0200);
    //err += test_ftruncate(0, 0000);
    
    err += test_open(0, O_RDONLY, 0);
    err += test_open(1, O_RDONLY, 0);
    err += test_open(1, O_RDWR, 0);
    err += test_open(1, O_WRONLY, 0);
    err += test_open(0, O_RDWR | O_CREAT, 0600);
    err += test_open(1, O_RDWR | O_CREAT, 0600);
    err += test_open(0, O_RDWR | O_CREAT | O_TRUNC, 0600);
    err += test_open(1, O_RDWR | O_CREAT | O_TRUNC, 0600);
    err += test_open(0, O_RDONLY | O_CREAT, 0600);
    err += test_open(0, O_RDONLY | O_CREAT, 0400);
    err += test_open(0, O_RDONLY | O_CREAT, 0200);
    err += test_open(0, O_RDONLY | O_CREAT, 0000);
    err += test_open(0, O_WRONLY | O_CREAT, 0600);
    err += test_open(0, O_WRONLY | O_CREAT, 0400);
    err += test_open(0, O_WRONLY | O_CREAT, 0200);
    err += test_open(0, O_WRONLY | O_CREAT, 0000);
    err += test_open(0, O_RDWR | O_CREAT, 0400);
    err += test_open(0, O_RDWR | O_CREAT, 0200);
    err += test_open(0, O_RDWR | O_CREAT, 0000);
    err += test_open(0, O_RDWR | O_CREAT | O_EXCL, 0600);
    err += test_open(1, O_RDWR | O_CREAT | O_EXCL, 0600);
    err += test_open(0, O_RDWR | O_CREAT | O_EXCL, 0000);
    err += test_open(1, O_RDWR | O_CREAT | O_EXCL, 0000);
    
    //err += test_open_acc(O_RDONLY, 0600, 0);
    //err += test_open_acc(O_WRONLY, 0600, 0);
    //err += test_open_acc(O_RDWR,   0600, 0);
    //err += test_open_acc(O_RDONLY, 0400, 0);
    //err += test_open_acc(O_WRONLY, 0200, 0);
    //if(!is_root) {
    //    err += test_open_acc(O_RDONLY | O_TRUNC, 0400, EACCES);
    //    err += test_open_acc(O_WRONLY, 0400, EACCES);
    //    err += test_open_acc(O_RDWR,   0400, EACCES);
    //    err += test_open_acc(O_RDONLY, 0200, EACCES);
    //    err += test_open_acc(O_RDWR,   0200, EACCES);
    //    err += test_open_acc(O_RDONLY, 0000, EACCES);
    //    err += test_open_acc(O_WRONLY, 0000, EACCES);
    //    err += test_open_acc(O_RDWR,   0000, EACCES);
    //}
    //err += test_create_ro_dir(O_CREAT);
    //err += test_create_ro_dir(O_CREAT | O_EXCL);
    //err += test_create_ro_dir(O_CREAT | O_WRONLY);
    //err += test_create_ro_dir(O_CREAT | O_TRUNC);
    //err += test_copy_file_range();
    
    err += test_read_add();
    err += test_write_add();
    err += test_mkdir_add();
    err += test_rmdir_add();
    err += test_readdir_add();

    unlink(testfile);
    unlink(testfile2);
    unlink(testsock);
    rmdir(testdir);
    rmdir(testdir2);

    if (err) {
        fprintf(stderr, "%i tests failed\n", -err);
        return 1;
    }

    return 0;
}
