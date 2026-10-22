/* Compile the real native adapter and transport against controlled OS calls. */
#define main nined_program_main
#include "../9d.c"
#undef main

#include <assert.h>
#include <stdarg.h>
#include <kernel.h>
#include <swis.h>
#include <unixlib/local.h>

static _kernel_oserror os_error = { 1, "fixture error" };
static unsigned file_calls, directory_calls;
static unsigned serial_attempts, serial_length, sleeps;
static unsigned char serial_bytes[256];
static int fail_directory;

char *__riscosify_std(const char *path, int flags, char *buffer,
                      size_t size, int *filetype) {
    (void)flags;
    (void)filetype;
    if(strcmp(path, "bad-conversion") == 0 || strlen(path) >= size)
        return NULL;
    strcpy(buffer, path);
    return buffer;
}

static int directory_path(const char *path) {
    return strcmp(path, "SCSI::0.$") == 0 ||
           strcmp(path, "SCSI::4.$") == 0 ||
           strcmp(path, "CDFS:") == 0 ||
           strcmp(path, "CDFS::1.$") == 0 ||
           strcmp(path, "SCSI::0.$.subdir") == 0;
}

_kernel_oserror *_kernel_swi(int number, const _kernel_swi_regs *input,
                              _kernel_swi_regs *output) {
    _kernel_swi_regs request = *input;
    const char *path = (const char *)request.r[1];
    *output = request;
    if(number == OS_File) {
        file_calls++;
        assert(request.r[0] == 5);
        if(strcmp(path, "NetFS:") == 0 || strcmp(path, "broken") == 0)
            return &os_error;
        output->r[0] = directory_path(path) ? 2 : 0;
        output->r[2] = output->r[3] = output->r[4] = output->r[5] = 0;
        if(strcmp(path, "SCSI::0.$.file/txt") == 0) {
            uint64_t timestamp = UINT64_C(220898880000) + 4200;
            output->r[0] = 1;
            output->r[2] = (int32_t)(UINT32_C(0xfff00000) |
                                     (uint32_t)(timestamp >> 32));
            output->r[3] = (int32_t)(uint32_t)timestamp;
            output->r[4] = 123;
            output->r[5] = 0x33;
        }
        return NULL;
    }
    assert(number == OS_GBPB && request.r[0] == 9);
    directory_calls++;
    if(fail_directory || strcmp(path, "broken") == 0)
        return &os_error;
    if(strcmp(path, "NetFS:") == 0 || request.r[4] >= 2) {
        output->r[3] = 0;
        output->r[4] = -1;
    } else {
        const char *name = request.r[4] == 0 ? "file/txt" : "subdir";
        assert(request.r[5] > (intptr_t)strlen(name));
        strcpy((char *)request.r[2], name);
        output->r[3] = 1;
        output->r[4]++;
    }
    return NULL;
}

_kernel_oserror *_swix(int number, unsigned mask, ...) {
    va_list args;
    (void)mask;
    va_start(args, mask);
    if(number == OS_FSControl) {
        unsigned fs;
        char *name;
        size_t size;
        assert(va_arg(args, int) == 33);
        fs = va_arg(args, unsigned);
        name = va_arg(args, char *);
        size = va_arg(args, size_t);
        snprintf(name, size, "%s", fs == 1 ? "SCSI" : fs == 2 ? "CDFS" :
                                  fs == 3 ? "NetFS" : "");
    } else if(number == SCSIFS_TestReady) {
        unsigned drive = va_arg(args, unsigned);
        int *drive_ready = va_arg(args, int *);
        *drive_ready = drive == 4 ? 2 : 0;
    } else {
        assert(number == CDFS_GetNumberOfDrives);
        *va_arg(args, int *) = 2;
    }
    va_end(args);
    return NULL;
}

int __os_423vdu(int byte) {
    if(serial_attempts++ % 3 != 2)
        return -1;
    assert(serial_length < sizeof(serial_bytes));
    serial_bytes[serial_length++] = (unsigned char)byte;
    return 0;
}

int usleep(useconds_t delay) {
    assert(delay == 1000);
    sleeps++;
    return 0;
}

static void test_native_metadata(void) {
    ResolvedPath path;
    struct stat before, after;
    unsigned queries;

    assert(namespace_init(NULL) == 0);
    assert(namespace.synthetic && namespace.nroots == 5);
    assert(namespace_resolve("/SCSI/file.txt", &path) == 0);
    assert(strcmp(path.native_path, "SCSI::0.$.file/txt") == 0);
    assert(platform_lstat(&path, &before) == 0);
    assert(S_ISREG(before.st_mode));
    assert((before.st_mode & 0777) == 0666);
    assert(before.st_size == 123 && before.st_mtime == 42);
    queries = file_calls;
    /* An invalid fd proves the matching-path case uses native metadata. */
    assert(platform_fstat(-1, &path, &before, &after) == 0);
    assert(file_calls == queries + 1);
    assert(before.st_ino == after.st_ino && before.st_dev == after.st_dev);
    {
        FILE *file = tmpfile();
        assert(file && ftruncate(fileno(file), 7) == 0);
        strcpy(path.native_path, "missing");
        assert(platform_fstat(fileno(file), &path, &before, &after) == 0);
        assert(after.st_size == 7);
        assert(before.st_ino == after.st_ino && before.st_dev == after.st_dev);
        assert(fclose(file) == 0);
    }
    assert(namespace_resolve("/SCSI", &path) == 0);
    assert(platform_lstat(&path, &after) == 0);
    assert(S_ISDIR(after.st_mode) && (after.st_mode & 0777) == 0755);
    assert(namespace_resolve("/NetFS", &path) == 0);
    queries = directory_calls;
    assert(platform_lstat(&path, &after) == 0);
    assert(S_ISDIR(after.st_mode) && directory_calls == queries + 1);
    strcpy(path.native_path, "missing");
    assert(platform_lstat(&path, &after) == -1 && errno == ENOENT);
    strcpy(path.native_path, "broken");
    assert(platform_lstat(&path, &after) == -1 && errno == EIO);
    strcpy(path.native_path, "bad-conversion");
    assert(platform_lstat(&path, &after) == -1 && errno == EILSEQ);
}

static void test_native_directory(void) {
    ResolvedPath path;
    PlatformDir *directory;
    struct dirent *entry;
    long cursor;
    unsigned queries;

    assert(namespace_resolve("/SCSI", &path) == 0);
    directory = platform_opendir(&path);
    assert(directory);
    entry = platform_readdir(directory);
    assert(entry && strcmp(entry->d_name, "file.txt") == 0);
    cursor = platform_telldir(directory);
    assert(cursor == 1);
    entry = platform_readdir(directory);
    assert(entry && strcmp(entry->d_name, "subdir") == 0);
    platform_seekdir(directory, cursor);
    entry = platform_readdir(directory);
    assert(entry && strcmp(entry->d_name, "subdir") == 0);
    assert(platform_readdir(directory) == NULL && errno == 0);
    queries = directory_calls;
    assert(platform_readdir(directory) == NULL && errno == 0);
    assert(directory_calls == queries);
    platform_rewinddir(directory);
    assert(platform_telldir(directory) == 0);
    fail_directory = 1;
    assert(platform_readdir(directory) == NULL && errno == EIO);
    assert(platform_telldir(directory) == 0);
    fail_directory = 0;
    entry = platform_readdir(directory);
    assert(entry && strcmp(entry->d_name, "file.txt") == 0);
    assert(platform_closedir(directory) == 0);
}

static void test_serial_retry(void) {
    unsigned char bytes[256];
    for(unsigned i = 0; i < sizeof(bytes); i++)
        bytes[i] = (unsigned char)i;
    stream_serial_output = 1;
    stream_read_fd = 10;
    stream_write_fd = 11;
    assert(stream_write(10, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    assert(serial_length == sizeof(bytes));
    assert(memcmp(bytes, serial_bytes, sizeof(bytes)) == 0);
    assert(serial_attempts == 3 * sizeof(bytes));
    assert(sleeps == 2 * sizeof(bytes));
}

int main(void) {
    test_native_metadata();
    test_native_directory();
    test_serial_retry();
    namespace_cleanup();
    puts("RISC OS native adapter fixtures passed");
    return 0;
}
