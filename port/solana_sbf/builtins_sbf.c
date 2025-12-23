// SBF-compatible builtins implementation
// Creates method Args directly to avoid deep call chains

#include "../../src/PikaObj.h"

#ifdef PIKA_SOLANA_SBF
#include <sol/cpi.h>
#include <sol/pubkey.h>
#endif

// Forward declarations
extern void builtins_print(PikaObj* self, PikaTuple* val, PikaDict* ops);
extern PikaObj* New_TinyObj(Args* args);

// ============================================================================
// Solana Clock Sysvar (for time builtin)
// ============================================================================
#ifdef PIKA_SOLANA_SBF
typedef struct {
    uint64_t slot;
    int64_t epoch_start_timestamp;
    uint64_t epoch;
    uint64_t leader_schedule_epoch;
    int64_t unix_timestamp;
} SolClock;

// Solana syscall to get clock sysvar (returns 0 on success)
// Using void* for compatibility with other modules that may have their own SolClock typedef
extern uint64_t sol_get_clock_sysvar(void* clock);
#endif

// ============================================================================
// print builtin
// ============================================================================
void _sbf_builtins_print(PikaObj* self, Args* args) {
    Arg* aVal = args_getArg(args, "val");
    PikaObj* val = (aVal != NULL) ? arg_getPtr(aVal) : NULL;
    Arg* aOps = args_getArg(args, "ops");
    PikaObj* ops = (aOps != NULL) ? arg_getPtr(aOps) : NULL;
    builtins_print(self, (PikaTuple*)val, (PikaDict*)ops);
}

// ============================================================================
// range builtin
// ============================================================================

// RangeData struct - must match PikaObj.h definition
typedef struct {
    int64_t start;
    int64_t end;
    int64_t step;
    int64_t i;
} SbfRangeData;

// Forward declaration
PikaObj* New_builtins_RangeObj(Args* args);

// Wrapper for builtins_range with proper Method signature
void _sbf_builtins_range(PikaObj* self, Args* args) {
    // Get the varargs tuple from args
    Arg* aAx = args_getArg(args, "ax");
    PikaTuple* ax = NULL;
    if (aAx != NULL && arg_getType(aAx) == ARG_TYPE_OBJECT) {
        ax = (PikaTuple*)arg_getPtr(aAx);
    }

    // Create RangeObj directly here
    Arg* aRangeObj = arg_newDirectObj(New_builtins_RangeObj);
    if (aRangeObj == NULL) {
        obj_setErrorCode(self, 1);
        return;
    }

    PikaObj* oRangeObj = arg_getPtr(aRangeObj);
    SbfRangeData rangeData;
    pika_platform_memset(&rangeData, 0, sizeof(rangeData));

    // Parse arguments
    int tupleSize = (ax != NULL) ? pikaTuple_getSize(ax) : 0;

    if (tupleSize == 1) {
        // range(end)
        rangeData.start = 0;
        rangeData.end = pikaTuple_getInt(ax, 0);
        rangeData.step = 1;
    } else if (tupleSize == 2) {
        // range(start, end)
        rangeData.start = pikaTuple_getInt(ax, 0);
        rangeData.end = pikaTuple_getInt(ax, 1);
        rangeData.step = 1;
    } else if (tupleSize >= 3) {
        // range(start, end, step)
        rangeData.start = pikaTuple_getInt(ax, 0);
        rangeData.end = pikaTuple_getInt(ax, 1);
        rangeData.step = pikaTuple_getInt(ax, 2);
    } else {
        // No arguments - return empty range
        rangeData.start = 0;
        rangeData.end = 0;
        rangeData.step = 1;
    }

    rangeData.i = rangeData.start;
    obj_setStruct(oRangeObj, "_", rangeData);
    method_returnArg(args, aRangeObj);
}

// ============================================================================
// time builtin - returns Solana clock unix_timestamp
// ============================================================================
void _sbf_builtins_time(PikaObj* self, Args* args) {
    (void)self;
#ifdef PIKA_SOLANA_SBF
    SolClock* clock = (SolClock*)pikaMalloc(sizeof(SolClock));
    if (clock == NULL) {
        method_returnInt(args, 0);
        return;
    }
    sol_get_clock_sysvar(clock);
    int64_t timestamp = clock->unix_timestamp;
    pikaFree(clock, sizeof(SolClock));
    method_returnInt(args, timestamp);
#else
    method_returnInt(args, 0);
#endif
}

// ============================================================================
// slot builtin - returns current Solana slot
// ============================================================================
void _sbf_builtins_slot(PikaObj* self, Args* args) {
    (void)self;
#ifdef PIKA_SOLANA_SBF
    SolClock* clock = (SolClock*)pikaMalloc(sizeof(SolClock));
    if (clock == NULL) {
        method_returnInt(args, 0);
        return;
    }
    sol_get_clock_sysvar(clock);
    int64_t slot = (int64_t)clock->slot;
    pikaFree(clock, sizeof(SolClock));
    method_returnInt(args, slot);
#else
    method_returnInt(args, 0);
#endif
}

// ============================================================================
// epoch builtin - returns current Solana epoch
// ============================================================================
void _sbf_builtins_epoch(PikaObj* self, Args* args) {
    (void)self;
#ifdef PIKA_SOLANA_SBF
    SolClock* clock = (SolClock*)pikaMalloc(sizeof(SolClock));
    if (clock == NULL) {
        method_returnInt(args, 0);
        return;
    }
    sol_get_clock_sysvar(clock);
    int64_t epoch = (int64_t)clock->epoch;
    pikaFree(clock, sizeof(SolClock));
    method_returnInt(args, epoch);
#else
    method_returnInt(args, 0);
#endif
}

// ============================================================================
// iter builtin - returns iterator for an object
// ============================================================================
void _sbf_builtins_iter(PikaObj* self, Args* args) {
    (void)self;
    Arg* aArg = args_getArg(args, "arg");
    if (aArg != NULL) {
        method_returnArg(args, arg_copy(aArg));
    } else {
        method_returnArg(args, arg_newNone());
    }
}

// ============================================================================
// bool builtin - convert to boolean
// ============================================================================
void _sbf_builtins_bool(PikaObj* self, Args* args) {
    (void)self;
    Arg* aVal = args_getArg(args, "val");

    // No argument or None -> False
    if (aVal == NULL) {
        method_returnInt(args, 0);
        return;
    }

    ArgType type = arg_getType(aVal);

    // None -> False
    if (type == ARG_TYPE_NONE) {
        method_returnInt(args, 0);
        return;
    }

    // Int -> False if 0, True otherwise
    if (type == ARG_TYPE_INT) {
        method_returnInt(args, arg_getInt(aVal) != 0 ? 1 : 0);
        return;
    }

    // Float -> False if 0.0, True otherwise
    if (type == ARG_TYPE_FLOAT) {
        method_returnInt(args, arg_getFloat(aVal) != 0.0f ? 1 : 0);
        return;
    }

    // String -> False if empty, True otherwise
    if (type == ARG_TYPE_STRING) {
        char* str = arg_getStr(aVal);
        method_returnInt(args, (str && str[0] != '\0') ? 1 : 0);
        return;
    }

    // Bytes -> False if empty, True otherwise
    if (type == ARG_TYPE_BYTES) {
        method_returnInt(args, arg_getBytesSize(aVal) > 0 ? 1 : 0);
        return;
    }

    // Object (list/tuple/dict) -> False if empty, True otherwise
    if (arg_isObject(aVal)) {
        PikaObj* obj = arg_getPtr(aVal);
        if (obj != NULL) {
            size_t size = pikaList_getSize(obj);
            method_returnInt(args, size > 0 ? 1 : 0);
            return;
        }
    }

    // Default: True for any other object
    method_returnInt(args, 1);
}

// ============================================================================
// abs builtin - absolute value
// ============================================================================
extern Arg* builtins_abs(PikaObj* self, Arg* val);

void _sbf_builtins_abs(PikaObj* self, Args* args) {
    Arg* aVal = args_getArg(args, "val");
    if (aVal == NULL) {
        method_returnArg(args, arg_newInt(0));
        return;
    }
    Arg* result = builtins_abs(self, aVal);
    if (result) {
        method_returnArg(args, result);
    } else {
        method_returnArg(args, arg_newInt(0));
    }
}

// ============================================================================
// len builtin - length of string/list/dict (direct SBF implementation)
// ============================================================================
void _sbf_builtins_len(PikaObj* self, Args* args) {
    (void)self;
    Arg* aArg = args_getArg(args, "arg");
    if (aArg == NULL) {
        method_returnInt(args, 0);
        return;
    }

    ArgType type = arg_getType(aArg);

    // String length
    if (type == ARG_TYPE_STRING) {
        char* str = arg_getStr(aArg);
        method_returnInt(args, (int)strGetSize(str));
        return;
    }

    // Bytes length
    if (type == ARG_TYPE_BYTES) {
        method_returnInt(args, (int)arg_getBytesSize(aArg));
        return;
    }

    // Object (list/tuple/dict/bytearray) - try multiple methods
    if (arg_isObject(aArg)) {
        PikaObj* obj = arg_getPtr(aArg);
        if (obj != NULL) {
            // First check if it has "raw" bytes (ByteArrayObj)
            Arg* rawArg = obj_getArg(obj, "raw");
            if (rawArg != NULL && arg_getType(rawArg) == ARG_TYPE_BYTES) {
                method_returnInt(args, (int)arg_getBytesSize(rawArg));
                return;
            }

            // Check if it's a dict (has "_keys" pointer)
            Args* keys = obj_getPtr(obj, "_keys");
            if (keys != NULL) {
                // Count items in keys linked list
                int count = 0;
                Arg* node = (Arg*)keys->firstNode;
                while (node != NULL) {
                    count++;
                    node = arg_getNext(node);
                }
                method_returnInt(args, count);
                return;
            }

            // Try pikaList_getSize which works for lists and tuples
            size_t size = pikaList_getSize(obj);
            method_returnInt(args, (int)size);
            return;
        }
    }

    // Unsupported type
    method_returnInt(args, -1);
}

// ============================================================================
// int builtin - convert to integer
// ============================================================================
extern Arg* builtins_int(PikaObj* self, Arg* arg, PikaTuple* base);

void _sbf_builtins_int(PikaObj* self, Args* args) {
    Arg* aArg = args_getArg(args, "arg");
    Arg* aBase = args_getArg(args, "base");
    PikaTuple* base = NULL;
    if (aBase != NULL && arg_getType(aBase) == ARG_TYPE_OBJECT) {
        base = (PikaTuple*)arg_getPtr(aBase);
    }
    if (aArg == NULL) {
        method_returnArg(args, arg_newInt(0));
        return;
    }
    Arg* result = builtins_int(self, aArg, base);
    if (result) {
        method_returnArg(args, result);
    } else {
        method_returnArg(args, arg_newInt(0));
    }
}

// ============================================================================
// max builtin - maximum value (direct C implementation for SBF)
// ============================================================================
void _sbf_builtins_max(PikaObj* self, Args* args) {
    (void)self;
    Arg* aVal = args_getArg(args, "val");
    PikaTuple* val = NULL;
    if (aVal != NULL && arg_getType(aVal) == ARG_TYPE_OBJECT) {
        val = (PikaTuple*)arg_getPtr(aVal);
    }
    if (val == NULL) {
        method_returnArg(args, arg_newInt(0));
        return;
    }

    int size = pikaTuple_getSize(val);
    if (size == 0) {
        method_returnArg(args, arg_newInt(0));
        return;
    }

    // Get first element as initial max
    Arg* maxArg = pikaTuple_getArg(val, 0);
    ArgType maxType = arg_getType(maxArg);
    int64_t maxInt = 0;
    pika_float maxFloat = 0;
    int isFloat = 0;

    if (maxType == ARG_TYPE_INT) {
        maxInt = arg_getInt(maxArg);
    } else if (maxType == ARG_TYPE_FLOAT) {
        maxFloat = arg_getFloat(maxArg);
        isFloat = 1;
    }

    // Compare with rest
    for (int i = 1; i < size; i++) {
        Arg* arg = pikaTuple_getArg(val, i);
        ArgType type = arg_getType(arg);

        if (type == ARG_TYPE_INT) {
            int64_t v = arg_getInt(arg);
            if (isFloat) {
                if ((pika_float)v > maxFloat) {
                    maxFloat = (pika_float)v;
                }
            } else {
                if (v > maxInt) {
                    maxInt = v;
                }
            }
        } else if (type == ARG_TYPE_FLOAT) {
            pika_float v = arg_getFloat(arg);
            if (!isFloat) {
                maxFloat = (pika_float)maxInt;
                isFloat = 1;
            }
            if (v > maxFloat) {
                maxFloat = v;
            }
        }
    }

    if (isFloat) {
        method_returnFloat(args, maxFloat);
    } else {
        method_returnInt(args, maxInt);
    }
}

// ============================================================================
// min builtin - minimum value (direct C implementation for SBF)
// ============================================================================
void _sbf_builtins_min(PikaObj* self, Args* args) {
    (void)self;
    Arg* aVal = args_getArg(args, "val");
    PikaTuple* val = NULL;
    if (aVal != NULL && arg_getType(aVal) == ARG_TYPE_OBJECT) {
        val = (PikaTuple*)arg_getPtr(aVal);
    }
    if (val == NULL) {
        method_returnArg(args, arg_newInt(0));
        return;
    }

    int size = pikaTuple_getSize(val);
    if (size == 0) {
        method_returnArg(args, arg_newInt(0));
        return;
    }

    // Get first element as initial min
    Arg* minArg = pikaTuple_getArg(val, 0);
    ArgType minType = arg_getType(minArg);
    int64_t minInt = 0;
    pika_float minFloat = 0;
    int isFloat = 0;

    if (minType == ARG_TYPE_INT) {
        minInt = arg_getInt(minArg);
    } else if (minType == ARG_TYPE_FLOAT) {
        minFloat = arg_getFloat(minArg);
        isFloat = 1;
    }

    // Compare with rest
    for (int i = 1; i < size; i++) {
        Arg* arg = pikaTuple_getArg(val, i);
        ArgType type = arg_getType(arg);

        if (type == ARG_TYPE_INT) {
            int64_t v = arg_getInt(arg);
            if (isFloat) {
                if ((pika_float)v < minFloat) {
                    minFloat = (pika_float)v;
                }
            } else {
                if (v < minInt) {
                    minInt = v;
                }
            }
        } else if (type == ARG_TYPE_FLOAT) {
            pika_float v = arg_getFloat(arg);
            if (!isFloat) {
                minFloat = (pika_float)minInt;
                isFloat = 1;
            }
            if (v < minFloat) {
                minFloat = v;
            }
        }
    }

    if (isFloat) {
        method_returnFloat(args, minFloat);
    } else {
        method_returnInt(args, minInt);
    }
}

// ============================================================================
// open builtin - opens a file (account) and returns FILEIO object
// ============================================================================

// Forward declarations for platform file functions
extern FILE* pika_platform_fopen(const char* filename, const char* modes);
extern int pika_platform_fclose(FILE* stream);
extern size_t pika_platform_fread(void* ptr, size_t size, size_t n, FILE* stream);
extern size_t pika_platform_fwrite(const void* ptr, size_t size, size_t n, FILE* stream);
extern int pika_platform_fseek(FILE* stream, long offset, int whence);
extern long pika_platform_ftell(FILE* stream);

// FILEIO methods
void _sbf_FILEIO_read(PikaObj* self, Args* args) {
    FILE* f = (FILE*)obj_getPtr(self, "_f");
    if (f == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Get size argument
    Arg* aSizeArg = args_getArg(args, "size");
    int size = 1024; // default
    if (aSizeArg != NULL) {
        PikaTuple* sizeTuple = (PikaTuple*)arg_getPtr(aSizeArg);
        if (sizeTuple && pikaTuple_getSize(sizeTuple) > 0) {
            size = (int)pikaTuple_getInt(sizeTuple, 0);
        }
    }

    if (size <= 0) size = 1024;

    // Allocate buffer
    uint8_t* buf = (uint8_t*)pika_platform_malloc((size_t)size + 1);
    if (!buf) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Read data
    size_t n = pika_platform_fread(buf, 1, (size_t)size, f);
    buf[n] = '\0';

    // Check mode
    char* mode = obj_getStr(self, "_mode");
    Arg* result;
    if (mode && strchr(mode, 'b')) {
        // Binary mode - return bytes
        result = arg_newBytes(buf, n);
    } else {
        // Text mode - return string
        result = arg_newStr((char*)buf);
    }

    pika_platform_free(buf);
    method_returnArg(args, result);
}

void _sbf_FILEIO_write(PikaObj* self, Args* args) {
    FILE* f = (FILE*)obj_getPtr(self, "_f");
    if (f == NULL) {
        method_returnInt(args, 0);
        return;
    }

    Arg* aData = args_getArg(args, "data");
    if (aData == NULL) {
        method_returnInt(args, 0);
        return;
    }

    size_t written = 0;
    ArgType type = arg_getType(aData);

    if (type == ARG_TYPE_STRING) {
        char* str = arg_getStr(aData);
        size_t len = strlen(str);
        written = pika_platform_fwrite(str, 1, len, f);
    } else if (type == ARG_TYPE_BYTES) {
        /* arg_getBytes returns pointer to [size_t size][data] - skip the size_t prefix */
        uint8_t* bytes = arg_getBytes(aData) + sizeof(size_t);
        size_t len = arg_getBytesSize(aData);
        written = pika_platform_fwrite(bytes, 1, len, f);
    } else if (argType_isObject(type)) {
        /* Handle bytearray objects - check for "raw" bytes attribute */
        PikaObj* obj = arg_getPtr(aData);
        if (obj != NULL) {
            Arg* rawArg = obj_getArg(obj, "raw");
            if (rawArg != NULL && arg_getType(rawArg) == ARG_TYPE_BYTES) {
                /* arg_getBytes returns pointer to [size_t size][data] - skip the size_t prefix */
                uint8_t* bytes = arg_getBytes(rawArg) + sizeof(size_t);
                size_t len = arg_getBytesSize(rawArg);
                written = pika_platform_fwrite(bytes, 1, len, f);
            }
        }
    }

    method_returnInt(args, (int)written);
}

void _sbf_FILEIO_seek(PikaObj* self, Args* args) {
    FILE* f = (FILE*)obj_getPtr(self, "_f");
    if (f == NULL) {
        method_returnInt(args, -1);
        return;
    }

    Arg* aOffset = args_getArg(args, "offset");
    int offset = aOffset ? (int)arg_getInt(aOffset) : 0;

    Arg* aWhence = args_getArg(args, "whence");
    int whence = 0; // SEEK_SET
    if (aWhence != NULL) {
        PikaTuple* whenceTuple = (PikaTuple*)arg_getPtr(aWhence);
        if (whenceTuple && pikaTuple_getSize(whenceTuple) > 0) {
            whence = (int)pikaTuple_getInt(whenceTuple, 0);
        }
    }

    pika_platform_fseek(f, offset, whence);
    method_returnInt(args, (int)pika_platform_ftell(f));
}

void _sbf_FILEIO_tell(PikaObj* self, Args* args) {
    FILE* f = (FILE*)obj_getPtr(self, "_f");
    if (f == NULL) {
        method_returnInt(args, -1);
        return;
    }
    method_returnInt(args, (int)pika_platform_ftell(f));
}

void _sbf_FILEIO_close(PikaObj* self, Args* args) {
    (void)args;
    FILE* f = (FILE*)obj_getPtr(self, "_f");
    if (f != NULL) {
        pika_platform_fclose(f);
        obj_setPtr(self, "_f", NULL);
    }
}

// FILEIO constructor
PikaObj* New_SbfFILEIO(Args* args) {
    (void)args;
    PikaObj* self = New_TinyObj(NULL);
    if (self == NULL) return NULL;

    self->refcnt = 1;
    obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);

    class_defineMethod(self, "read", "*size", (Method)_sbf_FILEIO_read);
    class_defineMethod(self, "write", "data", (Method)_sbf_FILEIO_write);
    class_defineMethod(self, "seek", "offset,*whence", (Method)_sbf_FILEIO_seek);
    class_defineMethod(self, "tell", "", (Method)_sbf_FILEIO_tell);
    class_defineMethod(self, "close", "", (Method)_sbf_FILEIO_close);

    return self;
}

// open() builtin implementation
void _sbf_builtins_open(PikaObj* self, Args* args) {
    (void)self;
    Arg* aPath = args_getArg(args, "path");
    Arg* aMode = args_getArg(args, "mode");

    if (aPath == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    char* path = arg_getStr(aPath);
    char* mode = aMode ? arg_getStr(aMode) : "r";

    // Open the file
    FILE* f = pika_platform_fopen(path, mode);
    if (f == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Create FILEIO object
    Arg* aFileObj = arg_newDirectObj(New_SbfFILEIO);
    if (aFileObj == NULL) {
        pika_platform_fclose(f);
        method_returnArg(args, arg_newNone());
        return;
    }

    PikaObj* fileObj = arg_getPtr(aFileObj);
    obj_setPtr(fileObj, "_f", f);
    obj_setStr(fileObj, "_mode", mode);

    method_returnArg(args, aFileObj);
}

// ============================================================================
// ZipObj - iterator for zip()
// ============================================================================

// Forward declaration
PikaObj* New_builtins_ZipObj(Args* args);

// ZipObj.__next__ implementation
void _sbf_ZipObj___next__(PikaObj* self, Args* args) {
    // Get actual self from args (needed for bytecode execution)
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* zipObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    // Get the stored iterables
    PikaObj* iterables = obj_getObj(zipObj, "_iterables");
    if (iterables == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    int numIterables = (int)pikaList_getSize(iterables);
    if (numIterables == 0) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Get current indices
    int* indices = (int*)obj_getPtr(zipObj, "_indices");
    if (indices == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Create result tuple
    PikaObj* resultTuple = New_pikaTuple();
    if (resultTuple == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Get one element from each iterable
    for (int i = 0; i < numIterables; i++) {
        Arg* iterableArg = pikaList_getArg(iterables, i);
        if (iterableArg == NULL || !arg_isObject(iterableArg)) {
            obj_deinit(resultTuple);
            method_returnArg(args, arg_newNone());
            return;
        }

        PikaObj* iterable = arg_getPtr(iterableArg);
        int idx = indices[i];

        // Get element at current index
        Arg* elem = pikaList_getArg(iterable, idx);
        if (elem == NULL) {
            // End of this iterable - zip stops at shortest
            obj_deinit(resultTuple);
            method_returnArg(args, arg_newNone());
            return;
        }

        // Add to result tuple
        pikaList_append(resultTuple, arg_copy(elem));
        indices[i]++;
    }

    method_returnObj(args, resultTuple);
}

// ZipObj.__iter__ implementation - returns self
void _sbf_ZipObj___iter__(PikaObj* self, Args* args) {
    // Get actual self from args (needed for bytecode execution)
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* zipObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    zipObj->refcnt++;
    method_returnObj(args, zipObj);
}

PikaObj* New_builtins_ZipObj(Args* args) {
    (void)args;
    PikaObj* self = New_TinyObj(NULL);
    if (self == NULL) {
        return NULL;
    }

    self->refcnt = 1;
    obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);

    class_defineMethod(self, "__iter__", "", (Method)_sbf_ZipObj___iter__);
    class_defineMethod(self, "__next__", "", (Method)_sbf_ZipObj___next__);

    return self;
}

// zip() builtin implementation
void _sbf_builtins_zip(PikaObj* self, Args* args) {
    (void)self;
    Arg* aIterables = args_getArg(args, "iterables");
    PikaTuple* iterables = NULL;
    if (aIterables != NULL && arg_getType(aIterables) == ARG_TYPE_OBJECT) {
        iterables = (PikaTuple*)arg_getPtr(aIterables);
    }

    int numIterables = (iterables != NULL) ? pikaTuple_getSize(iterables) : 0;

    // Create ZipObj
    Arg* aZipObj = arg_newDirectObj(New_builtins_ZipObj);
    if (aZipObj == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    PikaObj* zipObj = arg_getPtr(aZipObj);

    // Store iterables as a list
    PikaObj* iterablesList = New_pikaList();
    if (iterablesList == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Allocate indices array
    int* indices = NULL;
    if (numIterables > 0) {
        indices = (int*)pikaMalloc(numIterables * sizeof(int));
        if (indices == NULL) {
            obj_deinit(iterablesList);
            method_returnArg(args, arg_newNone());
            return;
        }
        pika_platform_memset(indices, 0, numIterables * sizeof(int));
    }

    // Copy each iterable
    for (int i = 0; i < numIterables; i++) {
        Arg* iterableArg = pikaTuple_getArg(iterables, i);
        if (iterableArg != NULL) {
            pikaList_append(iterablesList, arg_copy(iterableArg));
        }
    }

    obj_setObj(zipObj, "_iterables", iterablesList);
    obj_setPtr(zipObj, "_indices", indices);
    obj_setInt(zipObj, "_num", numIterables);

    method_returnArg(args, aZipObj);
}

// ============================================================================
// ByteArrayObj - bytearray type
// ============================================================================

// Forward declaration
PikaObj* New_builtins_ByteArrayObj(Args* args);

// ByteArrayObj.__init__ - initialize from int (size) or bytes/string
void _sbf_ByteArrayObj___init__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    Arg* aVal = args_getArg(args, "val");
    if (aVal == NULL) {
        // Empty bytearray
        obj_setBytes(baObj, "raw", NULL, 0);
        return;
    }

    ArgType type = arg_getType(aVal);
    if (type == ARG_TYPE_INT) {
        // Create bytearray of size N filled with zeros
        int size = arg_getInt(aVal);
        if (size < 0) size = 0;
        if (size > 0) {
            uint8_t* data = (uint8_t*)pikaMalloc(size);
            if (data != NULL) {
                pika_platform_memset(data, 0, size);
                obj_setBytes(baObj, "raw", data, size);
                pikaFree(data, size);
            } else {
                obj_setBytes(baObj, "raw", NULL, 0);
            }
        } else {
            obj_setBytes(baObj, "raw", NULL, 0);
        }
    } else if (type == ARG_TYPE_STRING) {
        // Create bytearray from string
        char* str = arg_getStr(aVal);
        int len = strGetSize(str);
        obj_setBytes(baObj, "raw", (uint8_t*)str, len);
    } else if (type == ARG_TYPE_BYTES) {
        // Create bytearray from bytes
        uint8_t* bytes = arg_getBytes(aVal);
        size_t size = arg_getBytesSize(aVal);
        obj_setBytes(baObj, "raw", bytes, size);
    } else if (argType_isObject(type)) {
        // Try to create from list/tuple
        PikaObj* obj = arg_getPtr(aVal);
        int size = (int)pikaList_getSize(obj);
        if (size > 0) {
            uint8_t* data = (uint8_t*)pikaMalloc(size);
            if (data != NULL) {
                for (int i = 0; i < size; i++) {
                    data[i] = (uint8_t)pikaList_getInt(obj, i);
                }
                obj_setBytes(baObj, "raw", data, size);
                pikaFree(data, size);
            } else {
                obj_setBytes(baObj, "raw", NULL, 0);
            }
        } else {
            obj_setBytes(baObj, "raw", NULL, 0);
        }
    } else {
        obj_setBytes(baObj, "raw", NULL, 0);
    }
}

// ByteArrayObj.__len__
void _sbf_ByteArrayObj___len__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;
    int len = obj_getBytesSize(baObj, "raw");
    method_returnInt(args, len);
}

// ByteArrayObj.__getitem__
void _sbf_ByteArrayObj___getitem__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;
    Arg* aKey = args_getArg(args, "__key");

    uint8_t* data = obj_getBytes(baObj, "raw");
    int len = obj_getBytesSize(baObj, "raw");
    int key = arg_getInt(aKey);

    // Handle negative indices
    if (key < 0) {
        key = len + key;
    }

    if (key >= 0 && key < len && data != NULL) {
        method_returnInt(args, data[key]);
    } else {
        method_returnInt(args, 0);
    }
}

// ByteArrayObj.__setitem__
void _sbf_ByteArrayObj___setitem__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;
    Arg* aKey = args_getArg(args, "__key");
    Arg* aVal = args_getArg(args, "__val");

    uint8_t* data = obj_getBytes(baObj, "raw");
    int len = obj_getBytesSize(baObj, "raw");
    int key = arg_getInt(aKey);
    int val = arg_getInt(aVal);

    // Handle negative indices
    if (key < 0) {
        key = len + key;
    }

    if (key >= 0 && key < len && data != NULL) {
        data[key] = (uint8_t)val;
    }
}

// ByteArrayObj.__iter__
void _sbf_ByteArrayObj___iter__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;
    obj_setInt(baObj, "__iter_i", 0);
    baObj->refcnt++;
    method_returnObj(args, baObj);
}

// ByteArrayObj.__next__
void _sbf_ByteArrayObj___next__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    int iter_i = obj_getInt(baObj, "__iter_i");
    uint8_t* data = obj_getBytes(baObj, "raw");
    int len = obj_getBytesSize(baObj, "raw");

    if (iter_i < len && data != NULL) {
        method_returnInt(args, data[iter_i]);
        obj_setInt(baObj, "__iter_i", iter_i + 1);
    } else {
        method_returnArg(args, arg_newNone());
    }
}

// ByteArrayObj.decode - convert to string
void _sbf_ByteArrayObj_decode(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    uint8_t* data = obj_getBytes(baObj, "raw");
    int len = obj_getBytesSize(baObj, "raw");

    if (data != NULL && len > 0) {
        // Need to ensure null termination for string
        char* str = (char*)pikaMalloc(len + 1);
        if (str != NULL) {
            pika_platform_memcpy(str, data, len);
            str[len] = '\0';
            method_returnStr(args, str);
            pikaFree(str, len + 1);
        } else {
            method_returnStr(args, "");
        }
    } else {
        method_returnStr(args, "");
    }
}

// ByteArrayObj.__str__
void _sbf_ByteArrayObj___str__(PikaObj* self, Args* args) {
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* baObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    uint8_t* data = obj_getBytes(baObj, "raw");
    int len = obj_getBytesSize(baObj, "raw");

    // Format: bytearray(b'\xNN\xNN...')
    // Each byte needs 4 chars (\xNN), plus "bytearray(b'" (12) and "')" (2)
    int bufSize = 14 + len * 4 + 1;
    char* buf = (char*)pikaMalloc(bufSize);
    if (buf == NULL) {
        method_returnStr(args, "bytearray(b'')");
        return;
    }

    int pos = 0;
    pika_platform_memcpy(buf + pos, "bytearray(b'", 12);
    pos += 12;

    for (int i = 0; i < len && data != NULL; i++) {
        buf[pos++] = '\\';
        buf[pos++] = 'x';
        uint8_t hi = (data[i] >> 4) & 0x0F;
        uint8_t lo = data[i] & 0x0F;
        buf[pos++] = hi < 10 ? '0' + hi : 'a' + hi - 10;
        buf[pos++] = lo < 10 ? '0' + lo : 'a' + lo - 10;
    }

    buf[pos++] = '\'';
    buf[pos++] = ')';
    buf[pos] = '\0';

    method_returnStr(args, buf);
    pikaFree(buf, bufSize);
}

PikaObj* New_builtins_ByteArrayObj(Args* args) {
    (void)args;
    PikaObj* self = New_TinyObj(NULL);
    if (self == NULL) {
        return NULL;
    }

    self->refcnt = 1;
    obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);

    // Note: Don't define __init__ - initialization is done by the builtin caller
    class_defineMethod(self, "__len__", "", (Method)_sbf_ByteArrayObj___len__);
    class_defineMethod(self, "__getitem__", "__key", (Method)_sbf_ByteArrayObj___getitem__);
    class_defineMethod(self, "__setitem__", "__key,__val", (Method)_sbf_ByteArrayObj___setitem__);
    class_defineMethod(self, "__iter__", "", (Method)_sbf_ByteArrayObj___iter__);
    class_defineMethod(self, "__next__", "", (Method)_sbf_ByteArrayObj___next__);
    class_defineMethod(self, "decode", "", (Method)_sbf_ByteArrayObj_decode);
    class_defineMethod(self, "__str__", "", (Method)_sbf_ByteArrayObj___str__);

    return self;
}

// bytearray() builtin
void _sbf_builtins_bytearray(PikaObj* self, Args* args) {
    (void)self;

    // Create ByteArrayObj
    Arg* aObj = arg_newDirectObj(New_builtins_ByteArrayObj);
    if (aObj == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    PikaObj* baObj = arg_getPtr(aObj);

    // Get the optional argument (comes as tuple due to *val)
    Arg* aVal = args_getArg(args, "val");

    // Extract the actual value from the tuple wrapper
    Arg* actualVal = NULL;
    if (aVal != NULL && arg_getType(aVal) == ARG_TYPE_OBJECT) {
        PikaTuple* tup = (PikaTuple*)arg_getPtr(aVal);
        if (pikaTuple_getSize(tup) > 0) {
            actualVal = pikaTuple_getArg(tup, 0);
        }
    }

    // Initialize with the value if provided
    if (actualVal != NULL) {
        ArgType type = arg_getType(actualVal);
        if (type == ARG_TYPE_INT) {
            int size = arg_getInt(actualVal);
            if (size < 0) size = 0;
            if (size > 0) {
                uint8_t* data = (uint8_t*)pikaMalloc(size);
                if (data != NULL) {
                    pika_platform_memset(data, 0, size);
                    obj_setBytes(baObj, "raw", data, size);
                    pikaFree(data, size);
                } else {
                    obj_setBytes(baObj, "raw", NULL, 0);
                }
            } else {
                obj_setBytes(baObj, "raw", NULL, 0);
            }
        } else if (type == ARG_TYPE_STRING) {
            char* str = arg_getStr(actualVal);
            int len = strGetSize(str);
            obj_setBytes(baObj, "raw", (uint8_t*)str, len);
        } else if (type == ARG_TYPE_BYTES) {
            /* arg_getBytes returns pointer INCLUDING size header (size_t prefix)
             * We need to skip sizeof(size_t) to get actual data */
            uint8_t* rawBytes = arg_getBytes(actualVal);
            size_t size = arg_getBytesSize(actualVal);
            uint8_t* actualBytes = rawBytes + sizeof(size_t);
            obj_setBytes(baObj, "raw", actualBytes, size);
        } else if (argType_isObject(type)) {
            PikaObj* obj = arg_getPtr(actualVal);
            int size = (int)pikaList_getSize(obj);
            if (size > 0) {
                uint8_t* data = (uint8_t*)pikaMalloc(size);
                if (data != NULL) {
                    for (int i = 0; i < size; i++) {
                        Arg* elemArg = pikaList_getArg(obj, i);
                        int val = 0;
                        if (elemArg != NULL) {
                            val = arg_getInt(elemArg);
                        }
                        data[i] = (uint8_t)val;
                    }
                    obj_setBytes(baObj, "raw", data, size);
                    pikaFree(data, size);
                } else {
                    obj_setBytes(baObj, "raw", NULL, 0);
                }
            } else {
                obj_setBytes(baObj, "raw", NULL, 0);
            }
        } else {
            obj_setBytes(baObj, "raw", NULL, 0);
        }
    } else {
        // Empty bytearray
        obj_setBytes(baObj, "raw", NULL, 0);
    }

    method_returnArg(args, aObj);
}

// ============================================================================
// cpi builtin - Cross-Program Invocation (optimized)
// ============================================================================
//
// Usage: cpi(program_idx, accounts, data)
//   program_idx: int account index of target program
//   accounts:    list of int account indices
//   data:        bytes instruction data
//
// Returns: int (0 for success, non-zero for error)

#ifdef PIKA_SOLANA_SBF

extern SolAccountInfo* pika_get_cpi_accounts(void);
extern uint64_t pika_get_cpi_num_accounts(void);

void _sbf_builtins_cpi(PikaObj* self, Args* args) {
    (void)self;

    SolAccountInfo* accounts = pika_get_cpi_accounts();
    uint64_t num_accounts = pika_get_cpi_num_accounts();

    // Get arguments directly - no validation
    int program_idx = args_getInt(args, "program_id");
    PikaObj* accountsList = args_getPtr(args, "accounts");

    // Get data bytes
    Arg* aData = args_getArg(args, "data");
    uint8_t* data_ptr = arg_getBytes(aData) + sizeof(size_t);
    uint64_t data_len = arg_getBytesSize(aData);

    int numCpiAccounts = (int)pikaList_getSize(accountsList);

    // Stack array - no heap allocation (max 16 accounts)
    SolAccountMeta metas[16];

    for (int i = 0; i < numCpiAccounts; i++) {
        Arg* elem = pikaList_getArg(accountsList, i);
        int idx = 0;
        int is_writable = 0;
        int is_signer = 0;

        if (elem != NULL && arg_isObject(elem)) {
            // Element is a tuple: (account_idx, is_writable, is_signer)
            PikaObj* tuple = arg_getPtr(elem);
            idx = (int)pikaList_getInt(tuple, 0);
            is_writable = (int)pikaList_getInt(tuple, 1);
            is_signer = (int)pikaList_getInt(tuple, 2);
        } else {
            // Element is just an integer index - inherit flags from account
            idx = (int)pikaList_getInt(accountsList, i);
            is_writable = accounts[idx].is_writable;
            is_signer = accounts[idx].is_signer;
        }

        SolAccountInfo* acct = &accounts[idx];
        metas[i].pubkey = acct->key;
        metas[i].is_writable = is_writable;
        metas[i].is_signer = is_signer;
    }

    SolInstruction instruction = {
        .program_id = accounts[program_idx].key,
        .accounts = metas,
        .account_len = (uint64_t)numCpiAccounts,
        .data = data_ptr,
        .data_len = data_len
    };

    // Pass original account infos directly
    uint64_t result = sol_invoke(&instruction, accounts, (int)num_accounts);

    method_returnInt(args, (int64_t)result);
}

#else
void _sbf_builtins_cpi(PikaObj* self, Args* args) {
    (void)self;
    method_returnInt(args, -1);
}
#endif

// ============================================================================
// str builtin - convert to string
// ============================================================================
void _sbf_builtins_str(PikaObj* self, Args* args) {
    (void)self;
    Arg* aVal = args_getArg(args, "val");

    // No argument -> empty string
    if (aVal == NULL) {
        method_returnStr(args, "");
        return;
    }

    ArgType type = arg_getType(aVal);

    // None -> "None"
    if (type == ARG_TYPE_NONE) {
        method_returnStr(args, "None");
        return;
    }

    // String -> return as is
    if (type == ARG_TYPE_STRING) {
        method_returnStr(args, arg_getStr(aVal));
        return;
    }

    // Int -> convert to string
    if (type == ARG_TYPE_INT) {
        int64_t val = arg_getInt(aVal);
        char buf[24];
        int neg = 0;
        int pos = 0;

        if (val < 0) {
            neg = 1;
            val = -val;
        } else if (val == 0) {
            method_returnStr(args, "0");
            return;
        }

        // Build digits in reverse
        char tmp[24];
        int tmpPos = 0;
        while (val > 0) {
            tmp[tmpPos++] = '0' + (val % 10);
            val /= 10;
        }

        // Reverse into buf
        if (neg) buf[pos++] = '-';
        while (tmpPos > 0) {
            buf[pos++] = tmp[--tmpPos];
        }
        buf[pos] = '\0';

        method_returnStr(args, buf);
        return;
    }

    // Float -> convert to string
    if (type == ARG_TYPE_FLOAT) {
        pika_float val = arg_getFloat(aVal);
        char buf[32];

        // Handle negative
        int pos = 0;
        if (val < 0) {
            buf[pos++] = '-';
            val = -val;
        }

        // Get integer part
        int64_t intPart = (int64_t)val;
        pika_float fracPart = val - (pika_float)intPart;

        // Convert integer part
        char tmp[24];
        int tmpPos = 0;
        if (intPart == 0) {
            tmp[tmpPos++] = '0';
        } else {
            while (intPart > 0) {
                tmp[tmpPos++] = '0' + (intPart % 10);
                intPart /= 10;
            }
        }
        while (tmpPos > 0) {
            buf[pos++] = tmp[--tmpPos];
        }

        // Add decimal point and fractional part (6 digits)
        buf[pos++] = '.';
        for (int i = 0; i < 6; i++) {
            fracPart *= 10;
            int digit = (int)fracPart;
            buf[pos++] = '0' + digit;
            fracPart -= digit;
        }
        buf[pos] = '\0';

        method_returnStr(args, buf);
        return;
    }

    // Bool -> "True" or "False"
    if (type == ARG_TYPE_BOOL) {
        method_returnStr(args, arg_getBool(aVal) ? "True" : "False");
        return;
    }

    // Bytes -> hex representation
    if (type == ARG_TYPE_BYTES) {
        uint8_t* bytes = arg_getBytes(aVal) + sizeof(size_t);
        size_t size = arg_getBytesSize(aVal);

        // Format: b'\xNN\xNN...'
        int bufSize = 3 + size * 4 + 2;
        char* buf = (char*)pikaMalloc(bufSize);
        if (buf == NULL) {
            method_returnStr(args, "b''");
            return;
        }

        int pos = 0;
        buf[pos++] = 'b';
        buf[pos++] = '\'';

        for (size_t i = 0; i < size; i++) {
            buf[pos++] = '\\';
            buf[pos++] = 'x';
            uint8_t hi = (bytes[i] >> 4) & 0x0F;
            uint8_t lo = bytes[i] & 0x0F;
            buf[pos++] = hi < 10 ? '0' + hi : 'a' + hi - 10;
            buf[pos++] = lo < 10 ? '0' + lo : 'a' + lo - 10;
        }

        buf[pos++] = '\'';
        buf[pos] = '\0';

        method_returnStr(args, buf);
        pikaFree(buf, bufSize);
        return;
    }

    // Object -> return type indicator
    if (argType_isObject(type)) {
        method_returnStr(args, "<object>");
        return;
    }

    // Unknown type - return empty string
    method_returnStr(args, "");
}

// ============================================================================
// exec builtin - execute Python source code
// ============================================================================
extern void builtins_exec(PikaObj* self, char* code);

void _sbf_builtins_exec(PikaObj* self, Args* args) {
    Arg* aCode = args_getArg(args, "code");
    if (aCode == NULL) {
        return;
    }
    char* code = arg_getStr(aCode);
    if (code == NULL) {
        return;
    }
    builtins_exec(self, code);
}

// ============================================================================
// builtins constructor for SBF
// ============================================================================
PikaObj* New_builtins(Args* args) {
    (void)args;
    PikaObj* self = New_PikaObj(NULL);
    if (self == NULL) {
        return NULL;
    }

    self->refcnt = 1;
    obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);

    // Add builtin methods
    class_defineMethod(self, "print", "*val,**ops", (Method)_sbf_builtins_print);
    class_defineMethod(self, "range", "*ax", (Method)_sbf_builtins_range);
    class_defineMethod(self, "iter", "arg", (Method)_sbf_builtins_iter);
    class_defineMethod(self, "abs", "val", (Method)_sbf_builtins_abs);
    class_defineMethod(self, "bool", "val", (Method)_sbf_builtins_bool);
    class_defineMethod(self, "len", "arg", (Method)_sbf_builtins_len);
    class_defineMethod(self, "int", "arg,*base", (Method)_sbf_builtins_int);
    class_defineMethod(self, "str", "val", (Method)_sbf_builtins_str);
    class_defineMethod(self, "max", "*val", (Method)_sbf_builtins_max);
    class_defineMethod(self, "min", "*val", (Method)_sbf_builtins_min);
    class_defineMethod(self, "open", "path,mode", (Method)_sbf_builtins_open);
    class_defineMethod(self, "exec", "code", (Method)_sbf_builtins_exec);
    class_defineMethod(self, "zip", "*iterables", (Method)_sbf_builtins_zip);
    class_defineMethod(self, "bytearray", "*val", (Method)_sbf_builtins_bytearray);
    class_defineMethod(self, "cpi", "program_id,accounts,data", (Method)_sbf_builtins_cpi);

    return self;
}

// ============================================================================
// builtins.object constructor
// ============================================================================
PikaObj* New_builtins_object(Args* args) {
    (void)args;
    PikaObj* self = New_TinyObj(NULL);
    if (self) {
        self->refcnt = 1;
        obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);
    }
    return self;
}

// ============================================================================
// RangeObj - iterator for range()
// ============================================================================

// RangeObj.__next__ implementation
void _sbf_RangeObj___next__(PikaObj* self, Args* args) {
    (void)self;
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* rangeObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;

    SbfRangeData* rangeData = (SbfRangeData*)obj_getStruct(rangeObj, "_");
    if (rangeData == NULL) {
        method_returnArg(args, arg_newNone());
        return;
    }

    // Check if we've reached the end
    if (rangeData->step > 0) {
        if (rangeData->i >= rangeData->end) {
            method_returnArg(args, arg_newNone());
            return;
        }
    } else if (rangeData->step < 0) {
        if (rangeData->i <= rangeData->end) {
            method_returnArg(args, arg_newNone());
            return;
        }
    } else {
        // step == 0 is invalid
        method_returnArg(args, arg_newNone());
        return;
    }

    // Return current value and advance
    int64_t value = rangeData->i;
    rangeData->i += rangeData->step;
    method_returnInt(args, value);
}

// RangeObj.__iter__ implementation - returns self
void _sbf_RangeObj___iter__(PikaObj* self, Args* args) {
    (void)self;
    Arg* aSelf = args_getArg(args, "self");
    PikaObj* rangeObj = (aSelf != NULL) ? arg_getPtr(aSelf) : self;
    rangeObj->refcnt++;
    method_returnObj(args, rangeObj);
}

PikaObj* New_builtins_RangeObj(Args* args) {
    (void)args;
    PikaObj* self = New_TinyObj(NULL);
    if (self == NULL) {
        return NULL;
    }

    self->refcnt = 1;
    obj_setFlag(self, OBJ_FLAG_ALREADY_INIT);

    class_defineMethod(self, "__iter__", "", (Method)_sbf_RangeObj___iter__);
    class_defineMethod(self, "__next__", "", (Method)_sbf_RangeObj___next__);

    return self;
}

// ============================================================================
// StringObj stub
// ============================================================================
PikaObj* New_builtins_StringObj(Args* args) {
    (void)args;
    return NULL;
}
