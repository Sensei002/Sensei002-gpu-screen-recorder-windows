#include "../../include/codec_query/vulkan.h"
#include "../../include/log.h"
#include "../../include/utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <dlfcn.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define MAX_PHYSICAL_DEVICES 32

static const char *required_device_extensions[] = {
    "VK_KHR_external_memory_fd",
    "VK_KHR_external_semaphore_fd",
    "VK_KHR_video_encode_queue",
    "VK_KHR_video_queue",
    "VK_KHR_video_maintenance1",
    "VK_EXT_external_memory_dma_buf",
    "VK_EXT_external_memory_host",
    "VK_EXT_image_drm_format_modifier"
};
static int num_required_device_extensions = 8;

static void set_h264_max_resolution(PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR vkGetPhysicalDeviceVideoCapabilitiesKHR, VkPhysicalDevice physical_device, gsr_supported_video_codecs *video_codecs) {
#ifdef VK_KHR_video_encode_h264
    const VkVideoEncodeH264ProfileInfoKHR h264_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR,
        .pNext = NULL,
        .stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH
    };

    const VkVideoProfileInfoKHR video_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
        .pNext = &h264_profile, // Chain the codec-specific profile
        .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
        .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
        .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
        .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR
    };

    VkVideoEncodeH264CapabilitiesKHR encode_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR,
        .pNext = NULL
    };

    VkVideoCapabilitiesKHR video_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &encode_caps
    };

    if (vkGetPhysicalDeviceVideoCapabilitiesKHR(physical_device, &video_profile, &video_caps) == VK_SUCCESS) {
        video_codecs->h264.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->h264.max_resolution.y = video_caps.maxCodedExtent.height;
    }
#else
    (void)vkGetPhysicalDeviceVideoCapabilitiesKHR;
    (void)physical_device;
    (void)video_codecs;
#endif
}

static void set_hevc_max_resolution(PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR vkGetPhysicalDeviceVideoCapabilitiesKHR, VkPhysicalDevice physical_device, gsr_supported_video_codecs *video_codecs) {
#ifdef VK_KHR_video_encode_h265
    const VkVideoEncodeH265ProfileInfoKHR hevc_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR,
        .pNext = NULL,
        .stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN
    };

    const VkVideoProfileInfoKHR video_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
        .pNext = &hevc_profile, // Chain the codec-specific profile
        .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR,
        .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
        .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
        .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR
    };

    VkVideoEncodeH265CapabilitiesKHR encode_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_CAPABILITIES_KHR,
        .pNext = NULL
    };

    VkVideoCapabilitiesKHR video_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &encode_caps
    };

    if (vkGetPhysicalDeviceVideoCapabilitiesKHR(physical_device, &video_profile, &video_caps) == VK_SUCCESS) {
        video_codecs->hevc.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->hevc.max_resolution.y = video_caps.maxCodedExtent.height;

        video_codecs->hevc_hdr.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->hevc_hdr.max_resolution.y = video_caps.maxCodedExtent.height;

        video_codecs->hevc_10bit.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->hevc_10bit.max_resolution.y = video_caps.maxCodedExtent.height;
    }
#else
    (void)vkGetPhysicalDeviceVideoCapabilitiesKHR;
    (void)physical_device;
    (void)video_codecs;
#endif
}

static void set_av1_max_resolution(PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR vkGetPhysicalDeviceVideoCapabilitiesKHR, VkPhysicalDevice physical_device, gsr_supported_video_codecs *video_codecs) {
#ifdef VK_KHR_video_encode_av1
    const VkVideoEncodeAV1ProfileInfoKHR av1_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_PROFILE_INFO_KHR,
        .pNext = NULL,
        .stdProfile = STD_VIDEO_AV1_PROFILE_MAIN
    };

    const VkVideoProfileInfoKHR video_profile = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR,
        .pNext = &av1_profile, // Chain the codec-specific profile
        .videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR,
        .chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
        .lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
        .chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR
    };

    VkVideoEncodeAV1CapabilitiesKHR encode_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_AV1_CAPABILITIES_KHR,
        .pNext = NULL
    };

    VkVideoCapabilitiesKHR video_caps = {
        .sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR,
        .pNext = &encode_caps
    };

    if (vkGetPhysicalDeviceVideoCapabilitiesKHR(physical_device, &video_profile, &video_caps) == VK_SUCCESS) {
        video_codecs->av1.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->av1.max_resolution.y = video_caps.maxCodedExtent.height;

        video_codecs->av1_hdr.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->av1_hdr.max_resolution.y = video_caps.maxCodedExtent.height;

        video_codecs->av1_10bit.max_resolution.x = video_caps.maxCodedExtent.width;
        video_codecs->av1_10bit.max_resolution.y = video_caps.maxCodedExtent.height;
    }
#else
    (void)vkGetPhysicalDeviceVideoCapabilitiesKHR;
    (void)physical_device;
    (void)video_codecs;
#endif
}

bool gsr_get_supported_video_codecs_vulkan(gsr_supported_video_codecs *video_codecs, const char *card_path, int *device_index_ret, bool cleanup) {
    memset(video_codecs, 0, sizeof(*video_codecs));
    *device_index_ret = 0;

    bool success = false;
    void* libvulkan = NULL;
    VkInstance instance = NULL;
    VkPhysicalDevice physical_devices[MAX_PHYSICAL_DEVICES];
    VkDevice device = NULL;
    VkExtensionProperties *device_extensions = NULL;

    char render_path[128];
    if(!gsr_card_path_get_render_path(card_path, render_path)) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: failed to get /dev/dri/renderDXXX file from %s", card_path);
        return false;
    }

    libvulkan = dlopen("libvulkan.so.1", RTLD_NOW);
    if (!libvulkan) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: failed to load libvulkan.so.1, error: %s", dlerror());
        return false;
    }

    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
    if (!vkGetInstanceProcAddr) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: could not find vkGetInstanceProcAddr in libvulkan.so.1");
        goto done;
    }

    PFN_vkCreateInstance vkCreateInstance = (PFN_vkCreateInstance)vkGetInstanceProcAddr(NULL, "vkCreateInstance");
    if(!vkCreateInstance) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: could not find vkCreateInstance in libvulkan.so.1");
        goto done;
    }

    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "GPU Screen Recorder",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "GPU Screen Recorder",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo instance_create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info
    };

    if(vkCreateInstance(&instance_create_info, NULL, &instance) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: vkCreateInstance failed");
        goto done;
    }

    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = NULL;
    PFN_vkDestroyInstance vkDestroyInstance = NULL;
    PFN_vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceProperties2 = NULL;
    PFN_vkCreateDevice vkCreateDevice = NULL;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = NULL;
    PFN_vkDestroyDevice vkDestroyDevice = NULL;
    PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR vkGetPhysicalDeviceVideoCapabilitiesKHR = NULL;

    #define LOAD_INST(name) name = (PFN_##name)vkGetInstanceProcAddr(instance, #name); if(!name) { gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: could not find " #name " in libvulkan.so.1"); goto done; }

        LOAD_INST(vkEnumeratePhysicalDevices)
        LOAD_INST(vkDestroyInstance)
        LOAD_INST(vkGetPhysicalDeviceProperties2)
        LOAD_INST(vkCreateDevice)
        LOAD_INST(vkEnumerateDeviceExtensionProperties)
        LOAD_INST(vkDestroyDevice)
        LOAD_INST(vkGetPhysicalDeviceVideoCapabilitiesKHR)

    #undef LOAD_INST

    uint32_t num_devices = 0;
    if(vkEnumeratePhysicalDevices(instance, &num_devices, NULL) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: vkEnumeratePhysicalDevices (query num devices) failed");
        goto done;
    }

    if(num_devices == 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: no vulkan capable device found");
        goto done;
    }

    if(num_devices > MAX_PHYSICAL_DEVICES)
        num_devices = MAX_PHYSICAL_DEVICES;
    
    if(vkEnumeratePhysicalDevices(instance, &num_devices, physical_devices) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: vkEnumeratePhysicalDevices (get data) failed");
        goto done;
    }

    VkPhysicalDevice physical_device = NULL;
    char device_card_path[128];
    for(uint32_t i = 0; i < num_devices; ++i) {
        VkPhysicalDeviceDrmPropertiesEXT device_drm_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT
        };

        VkPhysicalDeviceProperties2 device_properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &device_drm_properties
        };
        vkGetPhysicalDeviceProperties2(physical_devices[i], &device_properties);

        if(!device_drm_properties.hasRender)
            continue;

        snprintf(device_card_path, sizeof(device_card_path), DRM_RENDER_DEV_NAME, DRM_DIR_NAME, (int)device_drm_properties.renderMinor);
        if(strcmp(device_card_path, render_path) == 0) {
            physical_device = physical_devices[i];
            *device_index_ret = i;
            break;
        }
    }

    if(!physical_device) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: failed to find a vulkan device that matches opengl device %s", card_path);
        goto done;
    }

    const VkDeviceCreateInfo device_create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount = num_required_device_extensions,
        .ppEnabledExtensionNames = required_device_extensions
    };

    if(vkCreateDevice(physical_device, &device_create_info, NULL, &device) != VK_SUCCESS) {
        //fprintf(stderr, "gsr error: gsr_get_supported_video_codecs_vulkan: vkCreateDevice failed. Device %s likely doesn't support vulkan video encoding\n", card_path);
        goto done;
    }

    uint32_t num_device_extensions = 0;
    if(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &num_device_extensions, NULL) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: vkEnumerateDeviceExtensionProperties (query num device extensions) failed");
        goto done;
    }

    device_extensions = calloc(num_device_extensions, sizeof(VkExtensionProperties));
    if(!device_extensions) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: failed to allocate %d device extensions", num_device_extensions);
        goto done;
    }

    if(vkEnumerateDeviceExtensionProperties(physical_device, NULL, &num_device_extensions, device_extensions) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_get_supported_video_codecs_vulkan: vkEnumerateDeviceExtensionProperties (get data) failed");
        goto done;
    }

    for(uint32_t i = 0; i < num_device_extensions; ++i) {
        if(strcmp(device_extensions[i].extensionName, "VK_KHR_video_encode_h264") == 0) {
            video_codecs->h264.supported = true;
        } else if(strcmp(device_extensions[i].extensionName, "VK_KHR_video_encode_h265") == 0) {
            // TODO: Verify if 10bit and hdr are actually supported
            video_codecs->hevc.supported = true;
            video_codecs->hevc_10bit.supported = true;
            video_codecs->hevc_hdr.supported = true;
        } else if(strcmp(device_extensions[i].extensionName, "VK_KHR_video_encode_av1") == 0) {
            // TODO: Verify if 10bit and hdr are actually supported
            video_codecs->av1.supported = true;
            video_codecs->av1_10bit.supported = true;
            video_codecs->av1_hdr.supported = true;
        }
    }

    set_h264_max_resolution(vkGetPhysicalDeviceVideoCapabilitiesKHR, physical_device, video_codecs);
    set_hevc_max_resolution(vkGetPhysicalDeviceVideoCapabilitiesKHR, physical_device, video_codecs);
    set_av1_max_resolution(vkGetPhysicalDeviceVideoCapabilitiesKHR, physical_device, video_codecs);

    success = true;

    done:
    if(device_extensions)
        free(device_extensions);
    if(cleanup) {
        if(device)
            vkDestroyDevice(device, NULL);
        if(instance)
            vkDestroyInstance(instance, NULL);
    }
    if(libvulkan)
        dlclose(libvulkan);
    return success;
}
