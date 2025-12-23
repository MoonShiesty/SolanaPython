/*
 * PikaPlatform_sbf.c - Solana BPF Platform implementation
 *
 * This file implements the platform layer for PikaPython on Solana SBF.
 * It provides memory allocation, string functions, and I/O stubs.
 *
 * Note: pika_sbf_config.h is force-included via compiler flag (-include).
 * It provides all stdio/stdlib declarations and variadic function overrides.
 */

#include <stdint.h>

/* Mark that we're implementing the platform - prevents extern declarations */
#define PIKA_BPF_IMPLEMENTING 1

#include "../../src/PikaObj.h"
#include "../../src/PikaPlatform.h"
#include "../../src/PikaVM.h"
#include "../../src/dataArgs.h"
#include "../../src/dataStrs.h"

#include <solana_sdk.h>
#include <sol/return_data.h>

/* Virtual filesystem for account-backed files */
#include "solana_vfs.h"

/*
 * =============================================================================
 * Heap layout for Solana SBF
 * =============================================================================
 * Solana provides 32KB heap at 0x300000000 by default, zero-initialized.
 * We use a bump allocator that grows downward from the top.
 *
 * Layout:
 * Bytes 0-7:       heap position pointer
 * Bytes 8-1031:    print line buffer (1024 bytes)
 * Bytes 1032-1039: VFS accounts pointer
 * Bytes 1040-1047: VFS accounts count
 * Bytes 1048-1559: VFS file table (8 entries × 64 bytes)
 * Bytes 1560-1583: CPI context (24 bytes: accounts ptr + count + program_id ptr)
 * Bytes 1584+:     bump allocator space
 *
 * Note: Programs can request larger heap (up to 256KB) via sol_request_heap_frame
 * but we stay within the default 32KB for compatibility.
 */

#define HEAP_START 0x300000000ULL
#define HEAP_SIZE (256 * 1024)
#define HEAP_RESERVED 1584
#define PRINT_LINE_BUFF ((char*)(HEAP_START + 8))
#define PRINT_LINE_BUFF_SIZE 1024

static uint64_t* get_pos_ptr(void) {
    return (uint64_t*)HEAP_START;
}

/*
 * =============================================================================
 * Memory allocation - bump allocator
 * =============================================================================
 */

void* pika_platform_malloc(size_t size) {
    if (size == 0) return (void*)0;
    size = (size + 7) & ~7;  /* 8-byte alignment */
    uint64_t* pos_ptr = get_pos_ptr();
    uint64_t pos = *pos_ptr;
    if (pos == 0) {
        pos = HEAP_START + HEAP_SIZE - 64;
        *pos_ptr = pos;
    }
    if (pos < HEAP_START + HEAP_RESERVED + size + 8) {
        sol_log("Error: OOM in pika_platform_malloc");
        return (void*)0;
    }
    pos -= size;
    *pos_ptr = pos;
    return (void*)pos;
}

void* pika_platform_calloc(size_t n, size_t s) {
    size_t total = n * s;
    if (total == 0) return (void*)0;
    unsigned char* p = (unsigned char*)pika_platform_malloc(total);
    if (!p) return (void*)0;
    /* Solana heap is zero-initialized and we never free, so no need to zero */
    return p;
}

void* pika_platform_realloc(void* ptr, size_t size) {
    (void)ptr;
    return pika_platform_malloc(size);
}

void pika_platform_free(void* ptr) {
    (void)ptr;
    /* Bump allocator - no free */
}

/* Standard library wrappers */
void* malloc(size_t size) { return pika_platform_malloc(size); }
void free(void* ptr) { pika_platform_free(ptr); }

/*
 * Get heap usage statistics
 * Returns bytes used, bytes remaining via pointers
 */
void pika_platform_get_heap_stats(size_t* used, size_t* remaining) {
    uint64_t* pos_ptr = get_pos_ptr();
    uint64_t pos = *pos_ptr;
    if (pos == 0) {
        /* Heap not initialized yet */
        if (used) *used = 0;
        if (remaining) *remaining = HEAP_SIZE - HEAP_RESERVED;
    } else {
        size_t heap_end = HEAP_START + HEAP_SIZE - 64;
        size_t heap_begin = HEAP_START + HEAP_RESERVED;
        if (used) *used = heap_end - pos;
        if (remaining) *remaining = pos - heap_begin;
    }
}

/*
 * =============================================================================
 * Memory operations
 * =============================================================================
 */

void* pika_platform_memset(void* m, int c, size_t n) {
    unsigned char* p = (unsigned char*)m;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)c;
    return m;
}

void* pika_platform_memcpy(void* d, const void* s, size_t n) {
    unsigned char* D = (unsigned char*)d;
    const unsigned char* S = (const unsigned char*)s;
    for (size_t i = 0; i < n; ++i) D[i] = S[i];
    return d;
}

int pika_platform_memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* A = a;
    const unsigned char* B = b;
    for (size_t i = 0; i < n; ++i) {
        if (A[i] != B[i]) return (int)A[i] - (int)B[i];
    }
    return 0;
}

void* pika_platform_memmove(void* d, void* s, size_t n) {
    unsigned char* D = d;
    unsigned char* S = s;
    if (D < S) {
        for (size_t i = 0; i < n; ++i) D[i] = S[i];
    } else if (D > S) {
        for (size_t i = n; i-- > 0;) D[i] = S[i];
    }
    return d;
}

/* Standard library wrappers */
void* memset(void* m, int c, size_t n) { return pika_platform_memset(m, c, n); }
void* memcpy(void* d, const void* s, size_t n) { return pika_platform_memcpy(d, s, n); }
int memcmp(const void* a, const void* b, size_t n) { return pika_platform_memcmp(a, b, n); }
void* memmove(void* d, const void* s, size_t n) { return pika_platform_memmove(d, (void*)s, n); }

/*
 * =============================================================================
 * String operations
 * =============================================================================
 */

size_t strlen(const char* s) {
    size_t n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++));
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; ++i) dst[i] = src[i];
    for (; i < n; ++i) dst[i] = '\0';
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i] || !a[i] || !b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

char* strchr(const char* s, int c) {
    for (; *s; ++s) {
        if (*s == (char)c) return (char*)s;
    }
    return c == 0 ? (char*)s : (char*)0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;
    for (; *s; ++s) {
        if (*s == (char)c) last = s;
    }
    return (char*)(c == 0 ? s : last);
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (const char* p = haystack; *p; ++p) {
        const char* a = p;
        const char* b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (!*b) return (char*)p;
    }
    return (char*)0;
}

char* strdup(const char* src) {
    size_t n = strlen(src);
    char* d = (char*)pika_platform_malloc(n + 1);
    if (!d) return (char*)0;
    memcpy(d, src, n);
    d[n] = '\0';
    return d;
}

char* strndup(const char* src, size_t n) {
    size_t len = 0;
    while (src[len] && len < n) len++;
    char* d = (char*)pika_platform_malloc(len + 1);
    if (!d) return (char*)0;
    memcpy(d, src, len);
    d[len] = '\0';
    return d;
}

int strGetSizeUtf8(char* str) {
    if (!str) return 0;
    return (int)strlen(str);
}

/*
 * =============================================================================
 * String to number conversions
 * =============================================================================
 */

long long strtoll(const char* nptr, char** endptr, int base) {
    if (!nptr) {
        if (endptr) *endptr = (char*)nptr;
        return 0;
    }
    const char* s = nptr;
    long long result = 0;
    int negative = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    /* Handle sign */
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Auto-detect base */
    if (base == 0) {
        if (*s == '0') {
            if (*(s+1) == 'x' || *(s+1) == 'X') { base = 16; s += 2; }
            else if (*(s+1) == 'b' || *(s+1) == 'B') { base = 2; s += 2; }
            else { base = 8; s++; }
        } else { base = 10; }
    } else if (base == 16 && *s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) {
        s += 2;
    } else if (base == 2 && *s == '0' && (*(s+1) == 'b' || *(s+1) == 'B')) {
        s += 2;
    } else if (base == 8 && *s == '0') {
        s++;
    }

    const char* start = s;
    while (*s) {
        int digit = -1;
        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'z') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') digit = *s - 'A' + 10;
        if (digit < 0 || digit >= base) break;
        result = result * base + digit;
        s++;
    }

    if (endptr) *endptr = (char*)(s == start ? nptr : s);
    return negative ? -result : result;
}

long strtol(const char* nptr, char** endptr, int base) {
    return (long)strtoll(nptr, endptr, base);
}

double strtod(const char* nptr, char** endptr) {
    if (!nptr) {
        if (endptr) *endptr = (char*)nptr;
        return 0.0;
    }
    const char* s = nptr;
    double result = 0.0;
    int negative = 0;

    /* Skip whitespace */
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;

    /* Handle sign */
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* Parse integer part */
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (double)(*s - '0');
        s++;
    }

    /* Parse decimal part */
    if (*s == '.') {
        s++;
        double divisor = 10.0;
        while (*s >= '0' && *s <= '9') {
            result = result + (double)(*s - '0') / divisor;
            divisor = divisor * 10.0;
            s++;
        }
    }

    /* Handle exponent (e.g., 1.5e10) */
    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_negative = 0;
        if (*s == '-') { exp_negative = 1; s++; }
        else if (*s == '+') { s++; }

        int exp = 0;
        while (*s >= '0' && *s <= '9') {
            exp = exp * 10 + (*s - '0');
            s++;
        }

        /* Apply exponent */
        double multiplier = 1.0;
        for (int i = 0; i < exp; i++) {
            multiplier = multiplier * 10.0;
        }
        if (exp_negative) {
            result = result / multiplier;
        } else {
            result = result * multiplier;
        }
    }

    if (endptr) *endptr = (char*)s;
    return negative ? -result : result;
}

/*
 * =============================================================================
 * I/O stubs
 * =============================================================================
 */

int puts(const char* s) { sol_log(s); return 0; }
int fflush(void* stream) { (void)stream; return 0; }
void abort(void) { while(1){} }
int pika_platform_putchar(char ch) { return (int)ch; }
void pika_putchar(char c) { pika_platform_putchar(c); }
int pika_platform_fflush(void* stream) { (void)stream; return 0; }

/*
 * File I/O - backed by Solana VFS
 * We encode fd as (FILE*)(fd + 1) so that fd=0 doesn't become NULL
 *
 * Path formats:
 *   "/sol/N"  - Open account by index N (fast, no base58 decode)
 *   "<pubkey>" - Open account by base58 pubkey (slow, expensive)
 */
#define FD_TO_FILE(fd) ((FILE*)(uintptr_t)((fd) + 1))
#define FILE_TO_FD(f) ((int)(uintptr_t)(f) - 1)

/* Simple atoi for index parsing - returns -1 if not a valid number */
static int simple_atoi(const char* s) {
    if (!s || !*s) return -1;
    int result = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return -1;  /* Not a digit */
        result = result * 10 + (*s - '0');
        s++;
    }
    return result;
}

FILE* pika_platform_fopen(const char* filename, const char* modes) {
    int fd;

    /* Check for /sol/ prefix */
    if (filename[0] == '/' && filename[1] == 's' && filename[2] == 'o' &&
        filename[3] == 'l' && filename[4] == '/') {
        const char* path_part = filename + 5;
        int index = simple_atoi(path_part);

        if (index >= 0) {
            /* /sol/N format - index-based access (fast) */
            fd = sol_vfs_open_by_index((uint64_t)index, modes);
        } else {
            /* /sol/<pubkey> format - base58 pubkey after /sol/ */
            fd = sol_vfs_open(path_part, modes);
        }
    } else {
        /* Raw base58 pubkey (no /sol/ prefix) */
        fd = sol_vfs_open(filename, modes);
    }

    if (fd < 0) return (FILE*)0;
    return FD_TO_FILE(fd);
}

int pika_platform_fclose(FILE* stream) {
    if (!stream) return -1;
    return sol_vfs_close(FILE_TO_FD(stream));
}

size_t pika_platform_fwrite(const void* ptr, size_t size, size_t n, FILE* stream) {
    if (!stream) return 0;
    return sol_vfs_write(ptr, size, n, FILE_TO_FD(stream));
}

size_t pika_platform_fread(void* ptr, size_t size, size_t n, FILE* stream) {
    if (!stream) return 0;
    return sol_vfs_read(ptr, size, n, FILE_TO_FD(stream));
}

int pika_platform_fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    return sol_vfs_seek(FILE_TO_FD(stream), offset, whence);
}

long pika_platform_ftell(FILE* stream) {
    if (!stream) return -1;
    return sol_vfs_tell(FILE_TO_FD(stream));
}

/*
 * =============================================================================
 * Printf implementation - fixed argument version
 * =============================================================================
 * SBF can't handle va_list, so we use fixed argument functions.
 * _pika_sprintf_impl5 is the core implementation supporting up to 4 format args.
 */

/* Forward declaration - implemented in PikaObj.c */
extern int _pika_sprintf_impl5(char* buff, const char* fmt, intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4);

void _pika_platform_printf_variadic(const char* fmt,
                                    intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
                                    intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8) {
    (void)a5; (void)a6; (void)a7; (void)a8;

    char* line_buff = PRINT_LINE_BUFF;
    _pika_sprintf_impl5(line_buff, fmt, a1, a2, a3, a4);
    size_t line_len = strlen(line_buff);

    /* Strip trailing \r\n for logging */
    while (line_len > 0 && (line_buff[line_len-1] == '\r' || line_buff[line_len-1] == '\n')) {
        line_buff[line_len-1] = '\0';
        line_len--;
    }

    /* Log to Solana (REPL-style: print goes to logs, not return data) */
    if (line_len > 0) {
        sol_log(line_buff);
    }
}

int _pika_sprintf_impl_variadic(char* buff, const char* fmt,
                                intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4,
                                intptr_t a5, intptr_t a6, intptr_t a7, intptr_t a8) {
    (void)a5; (void)a6; (void)a7; (void)a8;
    return _pika_sprintf_impl5(buff, fmt, a1, a2, a3, a4);
}

void _pika_platform_printf_impl1(const char* fmt) {
    _pika_platform_printf_variadic(fmt, 0, 0, 0, 0, 0, 0, 0, 0);
}
void _pika_platform_printf_impl2(const char* fmt, intptr_t a1) {
    _pika_platform_printf_variadic(fmt, a1, 0, 0, 0, 0, 0, 0, 0);
}
void _pika_platform_printf_impl3(const char* fmt, intptr_t a1, intptr_t a2) {
    _pika_platform_printf_variadic(fmt, a1, a2, 0, 0, 0, 0, 0, 0);
}
void _pika_platform_printf_impl4(const char* fmt, intptr_t a1, intptr_t a2, intptr_t a3) {
    _pika_platform_printf_variadic(fmt, a1, a2, a3, 0, 0, 0, 0, 0);
}

/*
 * =============================================================================
 * PikaPython integration functions
 * =============================================================================
 */

char* _strsFormat_variadic(void* buffs_void, uint16_t buffSize, const char* fmt, intptr_t arg1, intptr_t arg2) {
    Args* buffs = (Args*)buffs_void;
    char* buff = (char*)pika_platform_malloc(PIKA_SPRINTF_BUFF_SIZE);
    if (!buff) return NULL;
    _pika_sprintf_impl5(buff, fmt, arg1, arg2, 0, 0);
    char* result = strsCopy(buffs, buff);
    pika_platform_free(buff);
    return result;
}

void _obj_setSysOut_variadic(PikaObj* self, char* fmt,
                             intptr_t a1, intptr_t a2, intptr_t a3, intptr_t a4) {
    char* buff = (char*)pika_platform_malloc(PIKA_SPRINTF_BUFF_SIZE);
    if (!buff) return;
    _pika_sprintf_impl5(buff, fmt, a1, a2, a3, a4);
    obj_setStr(self, "sysOut", buff);
    pika_platform_free(buff);
}

/* New_builtins, New_builtins_object, New_builtins_RangeObj, New_builtins_StringObj
 * are now defined in builtins_sbf.c */

/*
 * =============================================================================
 * Platform stubs
 * =============================================================================
 */

int64_t pika_platform_get_tick(void) { return 0; }
void pika_platform_sleep_ms(uint32_t ms) { (void)ms; }
void pika_platform_sleep_us(uint32_t us) { (void)us; }
void pika_platform_disable_irq_handle(void) {}
void pika_platform_enable_irq_handle(void) {}
uint8_t pika_is_locked_pikaMemory(void) { return 0; }
void pika_platform_wait(void) {}
void pika_platform_error_handle(void) {}
void pika_platform_panic_handle(void) { while(1){} }

/* Threading stubs */
pika_platform_thread_t* pika_platform_thread_init(const char* name, void (*entry)(void*), void* const param, unsigned int stack_size, unsigned int priority, unsigned int tick) {
    (void)name; (void)entry; (void)param; (void)stack_size; (void)priority; (void)tick;
    return NULL;
}
void pika_platform_thread_exit(pika_platform_thread_t* thread) { (void)thread; }
void pika_platform_thread_destroy(pika_platform_thread_t* thread) { (void)thread; }
void pika_platform_thread_start(pika_platform_thread_t* thread) { (void)thread; }
void pika_platform_thread_stop(pika_platform_thread_t* thread) { (void)thread; }
void pika_platform_thread_yield(void) {}
uint64_t pika_platform_thread_self(void) { return 0; }

/* Filesystem stubs */
char* pika_platform_getcwd(char* buf, size_t size) { (void)buf; (void)size; return (char*)0; }
int pika_platform_chdir(const char* path) { (void)path; return -1; }
int pika_platform_rmdir(const char* pathname) { (void)pathname; return -1; }
int pika_platform_mkdir(const char* pathname, int mode) { (void)pathname; (void)mode; return -1; }
char* pika_platform_realpath(const char* path, char* resolved_path) { (void)path; (void)resolved_path; return (char*)0; }
int pika_platform_path_exists(const char* path) { (void)path; return 0; }
int pika_platform_path_isdir(const char* path) { (void)path; return 0; }
int pika_platform_path_isfile(const char* path) { (void)path; return 0; }
int pika_platform_remove(const char* pathname) { (void)pathname; return -1; }
int pika_platform_rename(const char* oldpath, const char* newpath) { (void)oldpath; (void)newpath; return -1; }
char** pika_platform_listdir(const char* path, int* count) { (void)path; (void)count; return (char**)0; }

/* Shell/REPL stubs */
int PikaStdData_FILEIO_init(PikaObj* self, char* path, char* mode) { (void)self; (void)path; (void)mode; return 0; }
void _do_pikaScriptShell(PikaObj* self, ShellConfig* cfg) { (void)self; (void)cfg; }
void pika_platform_clear(void) {}
char pika_platform_getchar(void) { return 0; }
void pika_platform_reboot(void) {}
int pika_platform_repl_recv(uint8_t* buff, size_t size, uint32_t timeout) { (void)buff; (void)size; (void)timeout; return 0; }
void shHistory_destroy(ShellHistory* self) { (void)self; }

/* Object constructor stubs */
PikaObj* New_PikaStdData_ByteArray(Args* args) { (void)args; return NULL; }
PikaObj* New_PikaStdData_String(Args* args) { (void)args; return NULL; }
PikaObj* New_PikaStdLib_SysObj(Args* args) { (void)args; return NULL; }
PikaObj* New_PikaStdData_FILEIO(Args* args) { (void)args; return NULL; }
/* New_builtins_RangeObj and New_builtins_StringObj are in builtins_sbf.c */

/* Variadic constructor stubs */
void* _pika_list_new(int n) { (void)n; return NULL; }
void* _pika_tuple_new(int n) { (void)n; return NULL; }
void* _pika_dict_new(int n) { (void)n; return NULL; }

/*
 * =============================================================================
 * Import hook for sol_ prefixed modules
 * =============================================================================
 * Intercepts import statements like:
 *   import sol_0       -> loads code from account index 0
 *   import sol_Base58  -> loads code from account by pubkey
 *
 * The module name after "sol_" is parsed as either:
 *   - A number (0-9) -> index-based access via /sol/N
 *   - A base58 string -> pubkey-based access
 */

/* Forward declarations */
extern PikaObj* New_TinyObj(Args* args);
extern VMParameters* obj_run(PikaObj* self, char* cmd);
extern int32_t obj_newDirectObj(PikaObj* self, char* objName, NewFun newFunPtr);
extern PikaObj* obj_getObj(PikaObj* obj, char* name);

/* Bytecode magic number (pyoc header) */
#define PIKA_BYTECODE_MAGIC 0x0f

/* Check if string is all digits */
static int is_all_digits(const char* s) {
    if (!s || !*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        s++;
    }
    return 1;
}

/*
 * Override obj_importModule to intercept sol_ prefixed imports
 * Returns 0 on success, -1 on failure
 */
int obj_importModule(PikaObj* self, char* module_name) {
    if (module_name == NULL) {
        return -1;
    }

    /* Check for sol_ prefix */
    if (module_name[0] == 's' && module_name[1] == 'o' &&
        module_name[2] == 'l' && module_name[3] == '_') {

        const char* path_part = module_name + 4;  /* Skip "sol_" */

        /* Build the VFS path */
        char path[64];
        if (is_all_digits(path_part)) {
            /* Index-based: sol_0 -> /sol/0 */
            path[0] = '/'; path[1] = 's'; path[2] = 'o'; path[3] = 'l'; path[4] = '/';
            int i = 5;
            const char* p = path_part;
            while (*p && i < 60) {
                path[i++] = *p++;
            }
            path[i] = '\0';
        } else {
            /* Pubkey-based: sol_Base58... -> Base58... */
            int i = 0;
            const char* p = path_part;
            while (*p && i < 60) {
                path[i++] = *p++;
            }
            path[i] = '\0';
        }

        /* Open the account file */
        FILE* f = pika_platform_fopen(path, "r");
        if (f == NULL) {
            return -1;
        }

        /* Get file size */
        pika_platform_fseek(f, 0, 2);  /* SEEK_END */
        long size = pika_platform_ftell(f);
        pika_platform_fseek(f, 0, 0);  /* SEEK_SET */

        if (size <= 0 || size > 32768) {
            pika_platform_fclose(f);
            return -1;
        }

        /* Allocate buffer for code */
        char* code = (char*)pika_platform_malloc((size_t)size + 1);
        if (code == NULL) {
            pika_platform_fclose(f);
            return -1;
        }

        /* Read the data (could be source or bytecode) */
        size_t n = pika_platform_fread(code, 1, (size_t)size, f);
        pika_platform_fclose(f);

        /* Use self (the calling context) */
        if (self == NULL) {
            pika_platform_free(code);
            return -1;
        }

        /* Create a module object on self */
        obj_newDirectObj(self, module_name, New_TinyObj);
        PikaObj* module = obj_getObj(self, module_name);

        if (module == NULL) {
            pika_platform_free(code);
            return -1;
        }

        /* Check if this is bytecode (starts with 0x0f magic) or source */
        if (n > 0 && (uint8_t)code[0] == PIKA_BYTECODE_MAGIC) {
            /* Bytecode - execute directly via VM (much faster) */
            pikaVM_runBytecode_ex_cfg cfg;
            pika_platform_memset(&cfg, 0, sizeof(cfg));
            cfg.globals = module;
            cfg.locals = module;
            cfg.name = module_name;
            cfg.vm_thread = self->vmFrame ? self->vmFrame->vm_thread : NULL;
            cfg.is_const_bytecode = pika_false;  /* We allocated it, not const */
            pikaVM_runByteCode_ex(module, (uint8_t*)code, &cfg);
            /* Note: don't free code - VM may reference it */
        } else {
            /* Source code - parse and execute with proper module context */
            code[n] = '\0';

            /* Use pikaVM_run_ex with explicit globals to ensure functions
             * are defined on the module object (not on __pikaMain).
             * This mirrors how bytecode execution handles it above. */
            pikaVM_run_ex_cfg src_cfg;
            pika_platform_memset(&src_cfg, 0, sizeof(src_cfg));
            src_cfg.globals = module;
            src_cfg.module_name = module_name;
            pikaVM_run_ex(module, code, &src_cfg);

            pika_platform_free(code);
        }
        return 0;  /* Success */
    }

    /* Handle 'math' module - native C implementation */
    if (module_name[0] == 'm' && module_name[1] == 'a' && module_name[2] == 't' &&
        module_name[3] == 'h' && module_name[4] == '\0') {
        extern PikaObj* New__math(Args* args);
        obj_newDirectObj(self, "math", New__math);
        return 0;
    }

    /* Not a sol_ or math module - try standard import */
    /* Note: This calls the original pika_getByteCodeFromModule path */
    extern uint8_t* pika_getByteCodeFromModule(char* module_name);
    extern PikaObj* obj_importModuleWithByteCode(PikaObj* self, char* name, uint8_t* byteCode);

    uint8_t* bytecode = pika_getByteCodeFromModule(module_name);
    if (bytecode == NULL) {
        return -1;
    }
    obj_importModuleWithByteCode(self, module_name, bytecode);
    return 0;
}

/*
 * =============================================================================
 * REPL hook override - captures expression results as return data
 * =============================================================================
 * This overrides the default pika_hook_unused_stack_arg (guarded by
 * PIKA_HOOK_UNUSED_STACK_ARG_OVERRIDE) to set the Solana return data
 * instead of printing to stdout.
 */
#ifdef PIKA_HOOK_UNUSED_STACK_ARG_OVERRIDE

/* Forward declare VM frame type (full definition in PikaVM.c) */
typedef struct PikaVMFrame PikaVMFrame;

/* Forward declarations for arg functions (defined in dataArg.c) */
extern ArgType arg_getType(Arg* self);
extern char* arg_getStr(Arg* self);
extern Arg* arg_toStrArg(Arg* arg);
extern void arg_deinit(Arg* self);

void pika_hook_unused_stack_arg(PikaVMFrame* vm, Arg* arg) {
    (void)vm;

    ArgType type = arg_getType(arg);

    /* For None type, output "None" string (Python REPL style) */
    if (type == ARG_TYPE_NONE) {
        sol_set_return_data((uint8_t*)"None", 4);
        return;
    }

    /* Convert to string representation (like Python REPL) */
    Arg* str_arg = arg_toStrArg(arg);
    if (!str_arg) return;

    char* str = arg_getStr(str_arg);
    if (str && str[0] != '\0') {
        /* Set as return data (overwrites previous, so last expression wins) */
        sol_set_return_data((uint8_t*)str, strlen(str));
    }

    arg_deinit(str_arg);
}

#endif /* PIKA_HOOK_UNUSED_STACK_ARG_OVERRIDE */
