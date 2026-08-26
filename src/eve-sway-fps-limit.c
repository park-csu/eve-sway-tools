#define _GNU_SOURCE
#include <vulkan/vk_layer.h>

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define EVE_LAYER_NAME "VK_LAYER_EVE_sway_tools"
#define STATE_REFRESH_NS 50000000LL

static PFN_vkGetInstanceProcAddr next_gipa;
static PFN_vkGetDeviceProcAddr next_gdpa;
static PFN_vkQueuePresentKHR next_queue_present;
static pthread_mutex_t limiter_lock = PTHREAD_MUTEX_INITIALIZER;
static int is_eve_process = -1;
static int64_t next_present_ns;
static int64_t state_checked_ns;
static int state_active_pid = -1;
static int state_inactive_fps = 10;
static int state_active_fps;

static int64_t monotonic_ns(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

static void sleep_until_ns(int64_t deadline) {
    struct timespec target = {
        .tv_sec = deadline / 1000000000LL,
        .tv_nsec = deadline % 1000000000LL,
    };
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &target, NULL) ==
           EINTR) {
    }
}

static int process_is_eve(void) {
    if (is_eve_process < 0) {
        char path[64];
        char name[32] = {0};
        snprintf(path, sizeof(path), "/proc/%d/comm", getpid());
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        ssize_t length = fd >= 0 ? read(fd, name, sizeof(name) - 1) : -1;
        if (fd >= 0) {
            close(fd);
        }
        if (length > 0) {
            name[length] = '\0';
            name[strcspn(name, "\r\n")] = '\0';
        }
        if (length > 0 && strcmp(name, "exefile.exe") == 0) {
            is_eve_process = 1;
        } else {
            is_eve_process = 0;
        }
    }
    return is_eve_process;
}

static void refresh_state(int64_t now) {
    if (now - state_checked_ns < STATE_REFRESH_NS) {
        return;
    }
    state_checked_ns = now;

    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (!runtime || !*runtime) {
        return;
    }

    char path[512];
        if (snprintf(path, sizeof(path), "%s/eve-sway-tools-fps-state", runtime) <= 0) {
        return;
    }

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }

    char state[96];
    ssize_t length = read(fd, state, sizeof(state) - 1);
    close(fd);
    if (length <= 0) {
        return;
    }
    state[length] = '\0';

    int active_pid;
    int inactive_fps;
    int active_fps;
    if (sscanf(state, "%d %d %d",
               &active_pid, &inactive_fps, &active_fps) == 3 &&
        inactive_fps > 0 && active_fps >= 0) {
        state_active_pid = active_pid;
        state_inactive_fps = inactive_fps;
        state_active_fps = active_fps;
    }
}

static void limit_present(void) {
    if (!process_is_eve()) {
        return;
    }

    pthread_mutex_lock(&limiter_lock);
    int64_t now = monotonic_ns();
    refresh_state(now);

    int fps = getpid() == state_active_pid
                  ? state_active_fps
                  : state_inactive_fps;
    if (fps <= 0) {
        next_present_ns = 0;
        pthread_mutex_unlock(&limiter_lock);
        return;
    }

    int64_t interval = 1000000000LL / fps;
    if (next_present_ns == 0 || now > next_present_ns + interval) {
        next_present_ns = now;
    }
    next_present_ns += interval;
    int64_t deadline = next_present_ns;
    pthread_mutex_unlock(&limiter_lock);

    sleep_until_ns(deadline);
}

static VkLayerInstanceCreateInfo *instance_chain_info(
    const VkInstanceCreateInfo *create_info, VkLayerFunction function) {
    VkLayerInstanceCreateInfo *chain =
        (VkLayerInstanceCreateInfo *)create_info->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
            chain->function == function) {
            return chain;
        }
        chain = (VkLayerInstanceCreateInfo *)chain->pNext;
    }
    return NULL;
}

static VkLayerDeviceCreateInfo *device_chain_info(
    const VkDeviceCreateInfo *create_info, VkLayerFunction function) {
    VkLayerDeviceCreateInfo *chain =
        (VkLayerDeviceCreateInfo *)create_info->pNext;
    while (chain) {
        if (chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
            chain->function == function) {
            return chain;
        }
        chain = (VkLayerDeviceCreateInfo *)chain->pNext;
    }
    return NULL;
}

VKAPI_ATTR VkResult VKAPI_CALL eve_CreateInstance(
    const VkInstanceCreateInfo *create_info,
    const VkAllocationCallbacks *allocator,
    VkInstance *instance) {
    VkLayerInstanceCreateInfo *chain =
        instance_chain_info(create_info, VK_LAYER_LINK_INFO);
    if (!chain || !chain->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa =
        chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create_instance =
        (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    if (!create_instance) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = create_instance(create_info, allocator, instance);
    if (result == VK_SUCCESS) {
        next_gipa = gipa;
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL eve_CreateDevice(
    VkPhysicalDevice physical_device,
    const VkDeviceCreateInfo *create_info,
    const VkAllocationCallbacks *allocator,
    VkDevice *device) {
    VkLayerDeviceCreateInfo *chain =
        device_chain_info(create_info, VK_LAYER_LINK_INFO);
    if (!chain || !chain->u.pLayerInfo) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    PFN_vkGetInstanceProcAddr gipa =
        chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa =
        chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    PFN_vkCreateDevice create_device =
        (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    if (!create_device) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result =
        create_device(physical_device, create_info, allocator, device);
    if (result == VK_SUCCESS) {
        next_gdpa = gdpa;
        next_queue_present =
            (PFN_vkQueuePresentKHR)gdpa(*device, "vkQueuePresentKHR");
    }
    return result;
}

VKAPI_ATTR VkResult VKAPI_CALL eve_QueuePresentKHR(
    VkQueue queue, const VkPresentInfoKHR *present_info) {
    limit_present();
    return next_queue_present(queue, present_info);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL eve_GetInstanceProcAddr(
    VkInstance instance, const char *name) {
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return (PFN_vkVoidFunction)eve_GetInstanceProcAddr;
    }
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)eve_CreateInstance;
    }
    if (strcmp(name, "vkCreateDevice") == 0) {
        return (PFN_vkVoidFunction)eve_CreateDevice;
    }
    if (strcmp(name, "vkQueuePresentKHR") == 0) {
        return (PFN_vkVoidFunction)eve_QueuePresentKHR;
    }
    return next_gipa ? next_gipa(instance, name) : NULL;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL eve_GetDeviceProcAddr(
    VkDevice device, const char *name) {
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)eve_GetDeviceProcAddr;
    }
    if (strcmp(name, "vkQueuePresentKHR") == 0) {
        return (PFN_vkVoidFunction)eve_QueuePresentKHR;
    }
    return next_gdpa ? next_gdpa(device, name) : NULL;
}

__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
    VkNegotiateLayerInterface *interface) {
    if (!interface ||
        interface->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (interface->loaderLayerInterfaceVersion > 2) {
        interface->loaderLayerInterfaceVersion = 2;
    }
    interface->pfnGetInstanceProcAddr = eve_GetInstanceProcAddr;
    interface->pfnGetDeviceProcAddr = eve_GetDeviceProcAddr;
    interface->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
