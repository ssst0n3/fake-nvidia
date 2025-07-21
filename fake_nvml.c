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

typedef struct nvmlMemory_st {
    unsigned long long total;
    unsigned long long free;
    unsigned long long used;
} nvmlMemory_t;

// --- Logging Utility ---
// ******************** FIX: MODIFIED LOG MACRO FOR CONDITIONAL LOGGING ********************
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
// *****************************************************************************************

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
    if (g_initialized) { return NVML_ERROR_ALREADY_INITIALIZED; }
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

// --- Symbol Aliases (Keep these for compatibility) ---
nvmlReturn_t nvmlInit(void) __attribute__((weak, alias("nvmlInit_v2")));
nvmlReturn_t nvmlDeviceGetCount(unsigned int* deviceCount) __attribute__((weak, alias("nvmlDeviceGetCount_v2")));
nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t* device) __attribute__((weak, alias("nvmlDeviceGetHandleByIndex_v2")));