/**
 * test_nvml_consistency.c
 *
 * Symbol-level consistency regression tests for the fake NVML stub
 * (requirement R4, requirements-nvml-enumeration-compat.md):
 *
 *   1. nvmlDeviceIsMigDeviceHandle(handle, &v) is always v==0 / NVML_SUCCESS;
 *   2. nvmlDeviceGetIndex(handle, &i) always returns the handle's logical
 *      index (equal to GetMinorNumber and to the /dev/nvidia<i> node index);
 *   3. GetUUID / GetIndex / GetMinorNumber are stable across repeated calls
 *      (the determinism prerequisite for CDI spec generation and replay);
 *   4. nvmlDeviceGetHandleByIndex(GetIndex(handle)) idempotently returns the
 *      same logical device (compared by UUID).
 *
 * Links against the built libfake_nvml.so (see Makefile test-nvml target).
 * Exit 0 on pass, 1 on any failure; each failure prints a FAIL line.
 */
#include <stdio.h>
#include <string.h>

// --- Minimal NVML type definitions (mirrors fake_nvml.c) ---
typedef enum nvmlReturn_enum {
    NVML_SUCCESS = 0,
    NVML_ERROR_UNINITIALIZED = 1,
    NVML_ERROR_INVALID_ARGUMENT = 2,
    NVML_ERROR_NOT_FOUND = 6
} nvmlReturn_t;

typedef struct nvmlDevice_st* nvmlDevice_t;

#define NVML_DEVICE_UUID_BUFFER_SIZE 80

// --- Stub API surface under test ---
extern nvmlReturn_t nvmlInit_v2(void);
extern nvmlReturn_t nvmlShutdown(void);
extern nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int* deviceCount);
extern nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t* device);
extern nvmlReturn_t nvmlDeviceGetUUID(nvmlDevice_t device, char* uuid, unsigned int length);
extern nvmlReturn_t nvmlDeviceGetIndex(nvmlDevice_t device, unsigned int* index);
extern nvmlReturn_t nvmlDeviceGetMinorNumber(nvmlDevice_t device, unsigned int* minorNumber);
extern nvmlReturn_t nvmlDeviceIsMigDeviceHandle(nvmlDevice_t device, unsigned int* isMigDevice);

#define FAKE_GPU_COUNT 4

static int g_failures = 0;

#define CHECK(cond, ...)                                                     \
    do {                                                                     \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("FAIL (line %d): ", __LINE__);                            \
            printf(__VA_ARGS__);                                             \
            printf("\n");                                                    \
        }                                                                    \
    } while (0)

int main(void) {
    // --- Guard: before init, the new symbols must not crash but report UNINITIALIZED ---
    nvmlDevice_t bogus = (nvmlDevice_t)0;
    unsigned int v = 99, i = 99;
    CHECK(nvmlDeviceIsMigDeviceHandle(bogus, &v) == NVML_ERROR_UNINITIALIZED,
          "IsMigDeviceHandle before init must return UNINITIALIZED");
    CHECK(nvmlDeviceGetIndex(bogus, &i) == NVML_ERROR_UNINITIALIZED,
          "GetIndex before init must return UNINITIALIZED");

    CHECK(nvmlInit_v2() == NVML_SUCCESS, "nvmlInit_v2 must succeed");

    // --- Guard: NULL-argument hardening on the two new symbols ---
    CHECK(nvmlDeviceIsMigDeviceHandle(NULL, &v) == NVML_ERROR_INVALID_ARGUMENT,
          "IsMigDeviceHandle(NULL, &v) must return INVALID_ARGUMENT");
    CHECK(nvmlDeviceGetIndex(NULL, &i) == NVML_ERROR_INVALID_ARGUMENT,
          "GetIndex(NULL, &i) must return INVALID_ARGUMENT");

    unsigned int deviceCount = 0;
    CHECK(nvmlDeviceGetCount_v2(&deviceCount) == NVML_SUCCESS,
          "GetCount_v2 must succeed");
    CHECK(deviceCount == FAKE_GPU_COUNT,
          "device count %u != expected %u", deviceCount, FAKE_GPU_COUNT);

    for (unsigned int d = 0; d < deviceCount; d++) {
        nvmlDevice_t handle = NULL;
        CHECK(nvmlDeviceGetHandleByIndex_v2(d, &handle) == NVML_SUCCESS && handle != NULL,
              "GetHandleByIndex_v2(%u) must return a handle", d);

        // Assertion 1: IsMigDeviceHandle is always v==0 / SUCCESS.
        v = 99;
        CHECK(nvmlDeviceIsMigDeviceHandle(handle, &v) == NVML_SUCCESS,
              "dev %u: IsMigDeviceHandle must return SUCCESS", d);
        CHECK(v == 0,
              "dev %u: IsMigDeviceHandle must report 0, got %u", d, v);

        // Assertion 2: GetIndex equals the handle's logical index, and agrees
        // with GetMinorNumber (the /dev/nvidia<i> node index).
        i = 99;
        unsigned int minor = 99;
        CHECK(nvmlDeviceGetIndex(handle, &i) == NVML_SUCCESS,
              "dev %u: GetIndex must return SUCCESS", d);
        CHECK(nvmlDeviceGetMinorNumber(handle, &minor) == NVML_SUCCESS,
              "dev %u: GetMinorNumber must return SUCCESS", d);
        CHECK(i == d, "dev %u: GetIndex returned %u", d, i);
        CHECK(minor == d, "dev %u: GetMinorNumber returned %u", d, minor);

        // Assertion 3: UUID / index / minor are stable across repeated calls.
        char uuid1[NVML_DEVICE_UUID_BUFFER_SIZE] = {0};
        char uuid2[NVML_DEVICE_UUID_BUFFER_SIZE] = {0};
        unsigned int i1 = 99, i2 = 99, m1 = 99, m2 = 99;
        CHECK(nvmlDeviceGetUUID(handle, uuid1, sizeof(uuid1)) == NVML_SUCCESS,
              "dev %u: GetUUID (first call) must succeed", d);
        CHECK(nvmlDeviceGetUUID(handle, uuid2, sizeof(uuid2)) == NVML_SUCCESS,
              "dev %u: GetUUID (second call) must succeed", d);
        CHECK(nvmlDeviceGetIndex(handle, &i1) == NVML_SUCCESS &&
              nvmlDeviceGetIndex(handle, &i2) == NVML_SUCCESS,
              "dev %u: repeated GetIndex must succeed", d);
        CHECK(nvmlDeviceGetMinorNumber(handle, &m1) == NVML_SUCCESS &&
              nvmlDeviceGetMinorNumber(handle, &m2) == NVML_SUCCESS,
              "dev %u: repeated GetMinorNumber must succeed", d);
        CHECK(strcmp(uuid1, uuid2) == 0,
              "dev %u: UUID not stable: %s vs %s", d, uuid1, uuid2);
        CHECK(i1 == i2 && i1 == d, "dev %u: GetIndex not stable", d);
        CHECK(m1 == m2 && m1 == d, "dev %u: GetMinorNumber not stable", d);

        // Assertion 4: GetHandleByIndex(GetIndex(handle)) idempotently
        // returns the same logical device (same UUID).
        unsigned int idx = 99;
        CHECK(nvmlDeviceGetIndex(handle, &idx) == NVML_SUCCESS,
              "dev %u: GetIndex for round-trip must succeed", d);
        nvmlDevice_t back = NULL;
        CHECK(nvmlDeviceGetHandleByIndex_v2(idx, &back) == NVML_SUCCESS && back != NULL,
              "dev %u: GetHandleByIndex_v2(%u) round-trip must succeed", d, idx);
        char uuidBack[NVML_DEVICE_UUID_BUFFER_SIZE] = {0};
        CHECK(nvmlDeviceGetUUID(back, uuidBack, sizeof(uuidBack)) == NVML_SUCCESS,
              "dev %u: GetUUID on round-tripped handle must succeed", d);
        CHECK(strcmp(uuidBack, uuid1) == 0,
              "dev %u: round-trip UUID mismatch: %s vs %s", d, uuidBack, uuid1);
    }

    // --- Shutdown guard: after shutdown the new symbols report UNINITIALIZED ---
    CHECK(nvmlShutdown() == NVML_SUCCESS, "nvmlShutdown must succeed");
    CHECK(nvmlDeviceIsMigDeviceHandle(NULL, &v) == NVML_ERROR_UNINITIALIZED,
          "IsMigDeviceHandle after shutdown must return UNINITIALIZED");
    CHECK(nvmlDeviceGetIndex(NULL, &i) == NVML_ERROR_UNINITIALIZED,
          "GetIndex after shutdown must return UNINITIALIZED");

    if (g_failures == 0) {
        printf("PASS: all NVML consistency assertions passed (%u devices)\n", deviceCount);
        return 0;
    }
    printf("FAIL: %d assertion(s) failed\n", g_failures);
    return 1;
}
