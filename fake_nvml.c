/**
 * fake_nvml.c
 *
 * A fake implementation of the NVIDIA Management Library (NVML) API.
 * This library can be preloaded using LD_PRELOAD to trick applications
 * into believing that NVIDIA GPUs are present on a system.
 *
 * Compilation:
 *   gcc -shared -fPIC -o libnvidia-ml.so.1 fake_nvml.c -ldl
 *
 * Usage (without logs):
 *   LD_PRELOAD=./libnvidia-ml.so.1 nvidia-container-cli info
 *
 * Usage (with logs):
 *   FAKE_NVML_LOG=1 LD_PRELOAD=./libnvidia-ml.so.1 nvidia-container-cli info
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <dlfcn.h>

// --- NVML Type Definitions (from nvml.h) ---
typedef enum nvmlReturn_enum {
    NVML_SUCCESS = 0,
    NVML_ERROR_UNINITIALIZED = 1,
    NVML_ERROR_INVALID_ARGUMENT = 2,
    NVML_ERROR_NOT_SUPPORTED = 3,
    NVML_ERROR_NO_PERMISSION = 4,
    NVML_ERROR_ALREADY_INITIALIZED = 5,
    NVML_ERROR_NOT_FOUND = 6,
    NVML_ERROR_INSUFFICIENT_SIZE = 7,
    NVML_ERROR_INSUFFICIENT_POWER = 8,
    NVML_ERROR_DRIVER_NOT_LOADED = 9,
    NVML_ERROR_TIMEOUT = 10,
    NVML_ERROR_FUNCTION_NOT_FOUND = 13,
    NVML_ERROR_UNKNOWN = 999
} nvmlReturn_t;

typedef struct nvmlDevice_st* nvmlDevice_t;

typedef enum nvmlBrandType_enum {
    NVML_BRAND_UNKNOWN = 0,
    NVML_BRAND_TESLA = 2
} nvmlBrandType_t;

typedef enum nvmlEnableState_enum {
    NVML_FEATURE_DISABLED = 0,
    NVML_FEATURE_ENABLED = 1
} nvmlEnableState_t;

#define NVML_DEVICE_NAME_BUFFER_SIZE 64
#define NVML_DEVICE_UUID_BUFFER_SIZE 80
#define NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE 80
#define NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE 32

typedef struct nvmlPciInfo_st {
    char busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pciDeviceId;
    unsigned int pciSubSystemId;
} nvmlPciInfo_t;

// --- NVML extended PCI info (consumed by nvmlDeviceGetPciInfoExt; from nvml.h) ---
// Distinct from nvmlPciInfo_t: carries a version field and PCI base/sub class codes.
typedef struct {
    unsigned int version;
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pciDeviceId;
    unsigned int pciSubSystemId;
    unsigned int baseClass;
    unsigned int subClass;
    char busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
} nvmlPciInfoExt_v1_t;

typedef nvmlPciInfoExt_v1_t nvmlPciInfoExt_t;

typedef struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvmlMemory_t;

// --- NVML UUID struct (consumed by nvmlDeviceGetHandleByUUIDV; from nvml.h) ---
#define NVML_DEVICE_UUID_ASCII_LEN 41
#define NVML_DEVICE_UUID_BINARY_LEN 16

typedef enum {
    NVML_UUID_TYPE_NONE = 0,
    NVML_UUID_TYPE_ASCII = 1,
    NVML_UUID_TYPE_BINARY = 2
} nvmlUUIDType_t;

typedef union {
    char str[NVML_DEVICE_UUID_ASCII_LEN];
    unsigned char bytes[NVML_DEVICE_UUID_BINARY_LEN];
} nvmlUUIDValue_t;

typedef struct {
    unsigned int version;
    unsigned int type;
    nvmlUUIDValue_t value;
} nvmlUUID_v1_t;

typedef nvmlUUID_v1_t nvmlUUID_t;

// --- Logging Utility ---
#define LOG(func_name, msg, ...)                                         \
    do {                                                                 \
        if (getenv("FAKE_NVML_LOG")) {                                   \
            time_t t = time(NULL);                                       \
            struct tm *tm_info = localtime(&t);                          \
            char time_buf[26];                                           \
            strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);         \
            fprintf(stderr, "[FAKE-GPU %s %d:%d %s] " msg "\n",           \
                    time_buf, getpid(), getpid(), func_name, ##__VA_ARGS__); \
        }                                                                \
    } while (0)

// --- Fake GPU State ---
#define FAKE_GPU_COUNT 4
#define FAKE_GPU_NAME "NVIDIA Tesla T4"
#define FAKE_DRIVER_VERSION "535.104.05"
#define FAKE_CUDA_VERSION 12020

typedef struct {
    int index;
    char name[NVML_DEVICE_NAME_BUFFER_SIZE];
    char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];
    nvmlPciInfo_t pci;
    nvmlDevice_t handle;
} fakeGpu_t;

static fakeGpu_t g_fake_gpus[FAKE_GPU_COUNT];
static int g_initialized = 0;

// --- NVML API Implementations ---

nvmlReturn_t nvmlInit_v2(void) {
    LOG(__func__, "enter");
    // Idempotent: the real libnvidia-ml returns NVML_SUCCESS when called again
    // without an intervening nvmlShutdown. libnvidia-sandboxutils.so (toolkit
    // >= 1.19.0) calls nvmlInit twice in a row; returning ALREADY_INITIALIZED
    // made it fail with ERROR_NVML_LIB_CALL. Re-initialization is a no-op here
    // because the fake GPU state is static.
    if (g_initialized) {
        LOG(__func__, "exit, already initialized (idempotent SUCCESS)");
        return NVML_SUCCESS;
    }
    for (int i = 0; i < FAKE_GPU_COUNT; ++i) {
        g_fake_gpus[i].index = i;
        snprintf(g_fake_gpus[i].name, NVML_DEVICE_NAME_BUFFER_SIZE, "%s", FAKE_GPU_NAME);
        snprintf(g_fake_gpus[i].uuid, NVML_DEVICE_UUID_BUFFER_SIZE, "GPU-%d-FAKE-UUID", i);
        snprintf(g_fake_gpus[i].pci.busId, NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE, "00000000:0%d:00.0", i + 1);
        g_fake_gpus[i].pci.domain = 0;
        g_fake_gpus[i].pci.bus = i + 1;
        g_fake_gpus[i].pci.device = 0;
        g_fake_gpus[i].pci.pciDeviceId = 0x1EB8;
        g_fake_gpus[i].pci.pciSubSystemId = 0x12A210DE;
        g_fake_gpus[i].handle = (nvmlDevice_t)&g_fake_gpus[i];
    }
    g_initialized = 1;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlShutdown(void) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    g_initialized = 0;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

const char* nvmlErrorString(nvmlReturn_t result) {
    LOG(__func__, "enter");
    LOG(__func__, "Translating error code: %d", (int)result);
    switch (result) {
        case NVML_SUCCESS: return "Success";
        case NVML_ERROR_UNINITIALIZED: return "Uninitialized";
        case NVML_ERROR_INVALID_ARGUMENT: return "Invalid Argument";
        case NVML_ERROR_NOT_SUPPORTED: return "Not Supported";
        case NVML_ERROR_NO_PERMISSION: return "No Permission";
        case NVML_ERROR_ALREADY_INITIALIZED: return "Already Initialized";
        case NVML_ERROR_NOT_FOUND: return "Not Found";
        case NVML_ERROR_INSUFFICIENT_SIZE: return "Insufficient Size";
        case NVML_ERROR_DRIVER_NOT_LOADED: return "Driver Not Loaded";
        case NVML_ERROR_FUNCTION_NOT_FOUND: return "Function Not Found";
        default: return "Unknown Error";
    }
}

nvmlReturn_t nvmlSystemGetDriverVersion(char* version, unsigned int length) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    strncpy(version, FAKE_DRIVER_VERSION, length);
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlSystemGetCudaDriverVersion(int* cudaDriverVersion) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    *cudaDriverVersion = FAKE_CUDA_VERSION;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int* deviceCount) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    *deviceCount = FAKE_GPU_COUNT;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t* device) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (index >= FAKE_GPU_COUNT) return NVML_ERROR_INVALID_ARGUMENT;
    *device = g_fake_gpus[index].handle;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

// ******************** FIX: ADDED MISSING SYMBOLS (toolkit >= 1.19.0) ********************
// nvidia-container-toolkit / libnvidia-container >= 1.19.0 reference these as undefined
// symbols (U) and resolve them from libnvidia-ml.so at runtime via cgo. Without them the
// toolkit's nvidia-ctk / nvidia-container-runtime-hook abort with:
//   symbol lookup error: undefined symbol: nvmlDeviceGetHandleByUUID
// Both perform the reverse of nvmlDeviceGetUUID: map a UUID back to a fake GPU handle.
//   - nvmlDeviceGetHandleByUUID  : public nvml.h API, UUID given as a C string.
//   - nvmlDeviceGetHandleByUUIDV : versioned variant (capital V, not _v2) absent from older
//     public headers but exported by the real driver; UUID given as a nvmlUUID_t struct whose
//     value union holds an ASCII string or raw bytes (see nvmlUUID_t above).
// Reverse-map an ASCII UUID string to its fake GPU handle (shared by the two APIs below).
static nvmlReturn_t fake_lookup_handle_by_uuid(const char *uuid, nvmlDevice_t *device) {
    for (int i = 0; i < FAKE_GPU_COUNT; ++i) {
        if (strcmp(uuid, g_fake_gpus[i].uuid) == 0) {
            *device = g_fake_gpus[i].handle;
            return NVML_SUCCESS;
        }
    }
    return NVML_ERROR_NOT_FOUND;
}

nvmlReturn_t nvmlDeviceGetHandleByUUID(const char *uuid, nvmlDevice_t *device) {
    LOG(__func__, "enter, uuid=%s", uuid ? uuid : "(null)");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (uuid == NULL || device == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    nvmlReturn_t result = fake_lookup_handle_by_uuid(uuid, device);
    LOG(__func__, "%s", result == NVML_SUCCESS ? "exit, matched" : "exit, UUID not found");
    return result;
}

nvmlReturn_t nvmlDeviceGetHandleByUUIDV(const nvmlUUID_t *uuid, nvmlDevice_t *device) {
    LOG(__func__, "enter, uuid type=%u", uuid ? uuid->type : 0u);
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (uuid == NULL || device == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    // The UUID value is a union: ASCII str[41] | binary bytes[16]. Our fake GPUs only carry
    // ASCII UUIDs ("GPU-<i>-FAKE-UUID"). Copy value.str into a NUL-terminated buffer so a
    // non-ASCII (binary) UUID is compared safely and simply cannot match.
    char ascii[NVML_DEVICE_UUID_ASCII_LEN];
    memcpy(ascii, uuid->value.str, NVML_DEVICE_UUID_ASCII_LEN);
    ascii[NVML_DEVICE_UUID_ASCII_LEN - 1] = '\0';
    nvmlReturn_t result = fake_lookup_handle_by_uuid(ascii, device);
    LOG(__func__, "%s", result == NVML_SUCCESS ? "exit, matched" : "exit, UUID not found");
    return result;
}
// ******************************************************************************************

nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char* name, unsigned int length) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    strncpy(name, gpu->name, length);
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetUUID(nvmlDevice_t device, char* uuid, unsigned int length) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    strncpy(uuid, gpu->uuid, length);
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetPciInfo(nvmlDevice_t device, nvmlPciInfo_t* pci) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    memcpy(pci, &gpu->pci, sizeof(nvmlPciInfo_t));
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

// ******************** FIX: ADDED MISSING PCI SYMBOLS (toolkit >= 1.19.0) ********************
// nvidia-container-toolkit / libnvidia-container >= 1.19.0 reference these as undefined
// symbols (U) and resolve them from libnvidia-ml.so via cgo. Without them every
// `docker run --runtime=nvidia --gpus=all ...` prints:
//   Couldn't load symbol: ... undefined symbol: nvmlDeviceGetPciInfo_v3
// (non-fatal, but noisy). All three fill the fake GPU's PCI info, consistent with
// nvmlDeviceGetPciInfo above. Per nvml.h:
//   - nvmlDeviceGetPciInfo_v2 / _v3 take nvmlPciInfo_t* (same struct as the base API;
//     _v3 is the current default, aliased to nvmlDeviceGetPciInfo by the header).
//   - nvmlDeviceGetPciInfoExt takes the distinct nvmlPciInfoExt_t* (versioned struct with
//     PCI base/sub class codes); we leave baseClass/subClass zero and do not touch the
//     caller's version field.
nvmlReturn_t nvmlDeviceGetPciInfo_v2(nvmlDevice_t device, nvmlPciInfo_t* pci) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    memcpy(pci, &gpu->pci, sizeof(nvmlPciInfo_t));
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetPciInfo_v3(nvmlDevice_t device, nvmlPciInfo_t* pci) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    memcpy(pci, &gpu->pci, sizeof(nvmlPciInfo_t));
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetPciInfoExt(nvmlDevice_t device, nvmlPciInfoExt_t* pci) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (pci == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    // Preserve the caller-set version field; fill the rest from the fake GPU.
    unsigned int version = pci->version;
    memset(pci, 0, sizeof(nvmlPciInfoExt_t));
    pci->version = version;
    pci->domain = gpu->pci.domain;
    pci->bus = gpu->pci.bus;
    pci->device = gpu->pci.device;
    pci->pciDeviceId = gpu->pci.pciDeviceId;
    pci->pciSubSystemId = gpu->pci.pciSubSystemId;
    snprintf(pci->busId, NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE, "%s", gpu->pci.busId);
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}
// ********************************************************************************************

nvmlReturn_t nvmlDeviceGetCudaComputeCapability(nvmlDevice_t device, int *major, int *minor) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (major == NULL || minor == NULL) return NVML_ERROR_INVALID_ARGUMENT;

    // Fake data for a Tesla T4 (Turing Architecture, CC 7.5)
    *major = 7;
    *minor = 5;

    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetBrand(nvmlDevice_t device, nvmlBrandType_t *type) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    *type = NVML_BRAND_TESLA;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetMinorNumber(nvmlDevice_t device, unsigned int* minorNumber) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    fakeGpu_t* gpu = (fakeGpu_t*)device;
    *minorNumber = gpu->index;
    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

// ******************** FIX: ADDED MISSING FUNCTION ********************
nvmlReturn_t nvmlDeviceGetMaxMigDeviceCount(nvmlDevice_t device, unsigned int* count) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (count == NULL) return NVML_ERROR_INVALID_ARGUMENT;

    // Our fake Tesla T4 does not support MIG.
    *count = 0;

    LOG(__func__, "exit");
    return NVML_SUCCESS;
}
// *********************************************************************

nvmlReturn_t nvmlDeviceGetMigCapability(nvmlDevice_t device, unsigned int* isMigCapable, unsigned int* isMigGpu) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (isMigCapable == NULL || isMigGpu == NULL) return NVML_ERROR_INVALID_ARGUMENT;

    // Tesla T4 does not support MIG.
    *isMigCapable = 0;
    *isMigGpu = 0;

    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetMigMode(nvmlDevice_t device, unsigned int *currentMode, unsigned int *pendingMode) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (currentMode == NULL || pendingMode == NULL) return NVML_ERROR_INVALID_ARGUMENT;

    // MIG is not enabled.
    *currentMode = NVML_FEATURE_DISABLED;
    *pendingMode = NVML_FEATURE_DISABLED;

    LOG(__func__, "exit");
    return NVML_SUCCESS;
}

// ******************** FIX: ADDED MISSING MIG SYMBOL (toolkit >= 1.19.0) ********************
// libnvidia-sandboxutils.so (shipped with nvidia-container-toolkit >= 1.19.0) dlopen's
// libnvidia-ml.so.1 and dlsym's nvmlDeviceGetMigDeviceHandleByIndex. The stub did not
// export it, so `nvidia-ctk cdi generate` printed:
//   ERROR: Couldn't load symbol: ... undefined symbol: nvmlDeviceGetMigDeviceHandleByIndex
//   Failed to init nvsandboxutils: ERROR_LIBRARY_LOAD; ignoring
// (non-fatal — sandboxutils init failure is ignored and CDI generation still succeeds —
// but it pollutes the output). The fake Tesla T4 does not support MIG, so there are no
// MIG devices to enumerate; return NVML_ERROR_NOT_FOUND for any index. Signature per nvml.h:
//   nvmlReturn_t nvmlDeviceGetMigDeviceHandleByIndex(nvmlDevice_t device,
//                                                    unsigned int index,
//                                                    nvmlDevice_t *migDevice);
nvmlReturn_t nvmlDeviceGetMigDeviceHandleByIndex(nvmlDevice_t device, unsigned int index,
                                                 nvmlDevice_t *migDevice) {
    LOG(__func__, "enter, index=%u", index);
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (migDevice == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    (void)device; // fake GPUs have no MIG devices; device validity is not checked further.
    LOG(__func__, "exit, no MIG devices (NOT_FOUND)");
    return NVML_ERROR_NOT_FOUND;
}

// ******************** FIX: ADDED REMAINING MIG SYMBOLS (toolkit >= 1.19.0) ********************
// libnvidia-sandboxutils.so (shipped with nvidia-container-toolkit >= 1.19.0) dlsym's these
// from libnvidia-ml.so.1. Without them `nvidia-ctk cdi generate` printed:
//   ERROR: Couldn't load symbol: ... undefined symbol: nvmlDeviceGetDeviceHandleFromMigDeviceHandle
//   Failed to init nvsandboxutils: ERROR_LIBRARY_LOAD; ignoring
// (non-fatal — sandboxutils init failure is ignored and CDI generation still succeeds —
// but it polluted the output). The fake Tesla T4 does not support MIG, so there are no MIG
// device handles and no GPU/compute instances; all three return NVML_ERROR_NOT_FOUND.
// Signatures per nvml.h:
//   nvmlReturn_t nvmlDeviceGetDeviceHandleFromMigDeviceHandle(nvmlDevice_t migDevice, nvmlDevice_t *device);
//   nvmlReturn_t nvmlDeviceGetComputeInstanceId(nvmlDevice_t device, unsigned int *id);
//   nvmlReturn_t nvmlDeviceGetGpuInstanceId(nvmlDevice_t device, unsigned int *id);
nvmlReturn_t nvmlDeviceGetDeviceHandleFromMigDeviceHandle(nvmlDevice_t migDevice, nvmlDevice_t *device) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (device == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    (void)migDevice; // no MIG device handles exist on fake GPUs.
    LOG(__func__, "exit, no MIG devices (NOT_FOUND)");
    return NVML_ERROR_NOT_FOUND;
}

nvmlReturn_t nvmlDeviceGetGpuInstanceId(nvmlDevice_t device, unsigned int *id) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (id == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    (void)device; // fake GPUs have no GPU instances.
    LOG(__func__, "exit, no GPU instances (NOT_FOUND)");
    return NVML_ERROR_NOT_FOUND;
}

nvmlReturn_t nvmlDeviceGetComputeInstanceId(nvmlDevice_t device, unsigned int *id) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (id == NULL) return NVML_ERROR_INVALID_ARGUMENT;
    (void)device; // fake GPUs have no compute instances.
    LOG(__func__, "exit, no compute instances (NOT_FOUND)");
    return NVML_ERROR_NOT_FOUND;
}
// ********************************************************************************************
// ********************************************************************************************

// ******************** ENHANCEMENT: ADDED COMMON FUNCTION FOR ROBUSTNESS ********************
nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t device, nvmlMemory_t *memory) {
    LOG(__func__, "enter");
    if (!g_initialized) return NVML_ERROR_UNINITIALIZED;
    if (memory == NULL) return NVML_ERROR_INVALID_ARGUMENT;

    // Fake data for a Tesla T4 (16 GB VRAM)
    memory->total = 16ULL * 1024 * 1024 * 1024; // 16 GiB
    memory->free = 15ULL * 1024 * 1024 * 1024;  // Fake 15 GiB free
    memory->used = 1ULL * 1024 * 1024 * 1024;   // Fake 1 GiB used

    LOG(__func__, "exit");
    return NVML_SUCCESS;
}
// *****************************************************************************************

// --- Symbol Aliases (Keep these for compatibility) ---
nvmlReturn_t nvmlInit(void) __attribute__((weak, alias("nvmlInit_v2")));
nvmlReturn_t nvmlDeviceGetCount(unsigned int* deviceCount) __attribute__((weak, alias("nvmlDeviceGetCount_v2")));
nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t* device) __attribute__((weak, alias("nvmlDeviceGetHandleByIndex_v2")));