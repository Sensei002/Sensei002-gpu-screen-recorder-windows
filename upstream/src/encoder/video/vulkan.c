#include "../../../include/encoder/video/vulkan.h"
#include "../../../include/log.h"
#include "../../../include/utils.h"
#include "../../../include/egl.h"

#include <libavcodec/avcodec.h>
#define VK_NO_PROTOTYPES
#include <libavutil/hwcontext_vulkan.h>

#include <vulkan/vulkan_core.h>

#define GL_HANDLE_TYPE_OPAQUE_FD_EXT       0x9586
#define GL_TEXTURE_TILING_EXT              0x9580
#define GL_OPTIMAL_TILING_EXT              0x9584
#define GL_LINEAR_TILING_EXT               0x9585
#define GL_DEDICATED_MEMORY_OBJECT_EXT     0x9581
#define GL_LAYOUT_GENERAL_EXT              0x958D

typedef struct {
    PFN_vkCreateImage                        vkCreateImage;
    PFN_vkDestroyImage                       vkDestroyImage;
    PFN_vkGetImageMemoryRequirements         vkGetImageMemoryRequirements;
    PFN_vkAllocateMemory                     vkAllocateMemory;
    PFN_vkFreeMemory                         vkFreeMemory;
    PFN_vkBindImageMemory                    vkBindImageMemory;
    PFN_vkGetMemoryFdKHR                     vkGetMemoryFdKHR;
    PFN_vkGetPhysicalDeviceMemoryProperties  vkGetPhysicalDeviceMemoryProperties;
    PFN_vkCreateCommandPool                  vkCreateCommandPool;
    PFN_vkDestroyCommandPool                 vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers             vkAllocateCommandBuffers;
    PFN_vkBeginCommandBuffer                 vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer                   vkEndCommandBuffer;
    PFN_vkCmdPipelineBarrier                 vkCmdPipelineBarrier;
    PFN_vkCmdCopyImage                       vkCmdCopyImage;
    PFN_vkCreateFence                        vkCreateFence;
    PFN_vkDestroyFence                       vkDestroyFence;
    PFN_vkResetFences                        vkResetFences;
    PFN_vkWaitForFences                      vkWaitForFences;
    PFN_vkGetDeviceQueue                     vkGetDeviceQueue;
    PFN_vkGetDeviceQueue2                    vkGetDeviceQueue2;
    PFN_vkQueueSubmit                        vkQueueSubmit;
    PFN_vkResetCommandBuffer                 vkResetCommandBuffer;
    PFN_vkCreateSemaphore                    vkCreateSemaphore;
    PFN_vkDestroySemaphore                   vkDestroySemaphore;
    PFN_vkGetSemaphoreFdKHR                  vkGetSemaphoreFdKHR;
} gsr_vk_funcs;

typedef struct {
    gsr_video_encoder_vulkan_params params;
    unsigned int target_textures[2];
    vec2i texture_sizes[2];
    AVBufferRef *device_ctx;

    gsr_vk_funcs vk;
    VkDevice vk_device;
    VkQueue vk_queue;

    /* Exportable images that GL renders into */
    VkImage export_images[2];
    VkDeviceMemory export_memory[2];
    VkDeviceSize export_memory_size[2];
    unsigned int gl_memory_objects[2];

    /* Vulkan command infrastructure for copying to encoder frame */
    VkCommandPool command_pool;
    VkCommandBuffer command_buffer;
    VkFence fence;
    bool fence_submitted; /* true if the fence was submitted and not yet waited on */

    /* GL→Vulkan semaphore (binary, exported to GL via GL_EXT_semaphore_fd) */
    VkSemaphore gl_ready_semaphore;
    unsigned int gl_semaphore;
} gsr_video_encoder_vulkan;

static bool gsr_vk_funcs_load(gsr_vk_funcs *vk, PFN_vkGetInstanceProcAddr get_inst_proc, VkInstance inst, VkDevice dev) {
    PFN_vkGetDeviceProcAddr get_dev_proc = (PFN_vkGetDeviceProcAddr)get_inst_proc(inst, "vkGetDeviceProcAddr");
    if(!get_dev_proc) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_vk_funcs_load: failed to load vkGetDeviceProcAddr");
        return false;
    }

#define LOAD_INST(name) vk->name = (PFN_##name)get_inst_proc(inst, #name); if(!vk->name) { gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_vk_funcs_load: failed to load " #name); return false; }
#define LOAD_DEV(name)  vk->name = (PFN_##name)get_dev_proc(dev, #name);   if(!vk->name) { gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_vk_funcs_load: failed to load " #name); return false; }

    LOAD_INST(vkGetPhysicalDeviceMemoryProperties)
    LOAD_DEV(vkCreateImage)
    LOAD_DEV(vkDestroyImage)
    LOAD_DEV(vkGetImageMemoryRequirements)
    LOAD_DEV(vkAllocateMemory)
    LOAD_DEV(vkFreeMemory)
    LOAD_DEV(vkBindImageMemory)
    LOAD_DEV(vkGetMemoryFdKHR)
    LOAD_DEV(vkCreateCommandPool)
    LOAD_DEV(vkDestroyCommandPool)
    LOAD_DEV(vkAllocateCommandBuffers)
    LOAD_DEV(vkBeginCommandBuffer)
    LOAD_DEV(vkEndCommandBuffer)
    LOAD_DEV(vkCmdPipelineBarrier)
    LOAD_DEV(vkCmdCopyImage)
    LOAD_DEV(vkCreateFence)
    LOAD_DEV(vkDestroyFence)
    LOAD_DEV(vkResetFences)
    LOAD_DEV(vkWaitForFences)
    LOAD_DEV(vkGetDeviceQueue)
    LOAD_DEV(vkGetDeviceQueue2)
    LOAD_DEV(vkQueueSubmit)
    LOAD_DEV(vkResetCommandBuffer)
    LOAD_DEV(vkCreateSemaphore)
    LOAD_DEV(vkDestroySemaphore)
    LOAD_DEV(vkGetSemaphoreFdKHR)

#undef LOAD_INST
#undef LOAD_DEV
    return true;
}

static bool gsr_video_encoder_vulkan_setup_context(gsr_video_encoder_vulkan *self, AVCodecContext *video_codec_context) {
    AVDictionary *options = NULL;

    char device_index_str[32];
    snprintf(device_index_str, sizeof(device_index_str), "%d", self->params.egl->vulkan_device_index);

    if(av_hwdevice_ctx_create(&self->device_ctx, AV_HWDEVICE_TYPE_VULKAN, device_index_str, options, 0) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_context: failed to create hardware device context");
        return false;
    }

    AVBufferRef *frame_context = av_hwframe_ctx_alloc(self->device_ctx);
    if(!frame_context) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_context: failed to create hwframe context");
        av_buffer_unref(&self->device_ctx);
        return false;
    }

    AVHWFramesContext *hw_frame_context = (AVHWFramesContext*)frame_context->data;
    hw_frame_context->width = video_codec_context->width;
    hw_frame_context->height = video_codec_context->height;
    hw_frame_context->sw_format = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
    hw_frame_context->format = video_codec_context->pix_fmt;
    hw_frame_context->device_ctx = (AVHWDeviceContext*)self->device_ctx->data;

    if (av_hwframe_ctx_init(frame_context) < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_context: failed to initialize hardware frame context (note: ffmpeg version needs to be > 4.0)");
        av_buffer_unref(&self->device_ctx);
        return false;
    }

    video_codec_context->hw_frames_ctx = av_buffer_ref(frame_context);
    av_buffer_unref(&frame_context);

    return true;
}

static AVVulkanDeviceContext* video_codec_context_get_vulkan_data(AVCodecContext *video_codec_context) {
    AVBufferRef *hw_frames_ctx = video_codec_context->hw_frames_ctx;
    if(!hw_frames_ctx)
        return NULL;

    AVHWFramesContext *hw_frame_context = (AVHWFramesContext*)hw_frames_ctx->data;
    AVHWDeviceContext *device_context = (AVHWDeviceContext*)hw_frame_context->device_ctx;
    if(device_context->type != AV_HWDEVICE_TYPE_VULKAN)
        return NULL;

    return (AVVulkanDeviceContext*)device_context->hwctx;
}

/* The flags that FFmpeg created the device queues with, which vkGetDeviceQueue2 has to be given to return the queue */
static VkDeviceQueueCreateFlags get_device_queue_create_flags(AVVulkanDeviceContext *vv) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(61, 1, 100)
    return vv->queue_flags;
#else
    (void)vv;
    return 0;
#endif
}

static int get_graphics_queue_family(AVVulkanDeviceContext *vv) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(59, 39, 100)
    for(int i = 0; i < vv->nb_qf; i++) {
        if(vv->qf[i].flags & VK_QUEUE_GRAPHICS_BIT)
            return vv->qf[i].idx;
    }
    /* Fall back to any queue that supports transfer */
    for(int i = 0; i < vv->nb_qf; i++) {
        if(vv->qf[i].flags & VK_QUEUE_TRANSFER_BIT)
            return vv->qf[i].idx;
    }
    return -1;
#else
    if(vv->queue_family_index >= 0)
        return vv->queue_family_index;
    if(vv->queue_family_tx_index >= 0)
        return vv->queue_family_tx_index;
    return -1;
#endif
}

static uint32_t get_memory_type_idx(VkPhysicalDevice pdev, const VkMemoryRequirements *mem_reqs, VkMemoryPropertyFlagBits prop_flags, PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties) {
    VkPhysicalDeviceMemoryProperties pdev_mem_props;
    vkGetPhysicalDeviceMemoryProperties(pdev, &pdev_mem_props);

    for(uint32_t i = 0; i < pdev_mem_props.memoryTypeCount; i++) {
        if((mem_reqs->memoryTypeBits & (1 << i)) &&
           (pdev_mem_props.memoryTypes[i].propertyFlags & prop_flags) == prop_flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

static bool create_exportable_image(
    const gsr_vk_funcs *vk,
    VkDevice dev,
    VkPhysicalDevice phys_dev,
    int width, int height,
    VkFormat format,
    VkImage *out_image,
    VkDeviceMemory *out_memory,
    VkDeviceSize *out_size)
{
    VkExternalMemoryImageCreateInfo ext_img_info = {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &ext_img_info,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { (uint32_t)width, (uint32_t)height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT       |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_STORAGE_BIT,
        .flags = VK_IMAGE_CREATE_ALIAS_BIT | VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    if(vk->vkCreateImage(dev, &img_info, NULL, out_image) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "create_exportable_image: vkCreateImage failed");
        return false;
    }

    VkMemoryRequirements mem_reqs;
    vk->vkGetImageMemoryRequirements(dev, *out_image, &mem_reqs);

    uint32_t mem_type_idx = get_memory_type_idx(phys_dev, &mem_reqs,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vk->vkGetPhysicalDeviceMemoryProperties);
    if(mem_type_idx == UINT32_MAX) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "create_exportable_image: no suitable memory type");
        vk->vkDestroyImage(dev, *out_image, NULL);
        *out_image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryDedicatedAllocateInfo ded_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = *out_image,
    };
    VkExportMemoryAllocateInfo exp_mem_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = &ded_info,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    VkMemoryAllocateInfo mem_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &exp_mem_info,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = mem_type_idx,
    };

    if(vk->vkAllocateMemory(dev, &mem_alloc_info, NULL, out_memory) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "create_exportable_image: vkAllocateMemory failed");
        vk->vkDestroyImage(dev, *out_image, NULL);
        *out_image = VK_NULL_HANDLE;
        return false;
    }

    if(vk->vkBindImageMemory(dev, *out_image, *out_memory, 0) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "create_exportable_image: vkBindImageMemory failed");
        vk->vkFreeMemory(dev, *out_memory, NULL);
        vk->vkDestroyImage(dev, *out_image, NULL);
        *out_memory = VK_NULL_HANDLE;
        *out_image = VK_NULL_HANDLE;
        return false;
    }

    *out_size = mem_reqs.size;
    return true;
}

static bool gsr_video_encoder_vulkan_setup_textures(gsr_video_encoder_vulkan *self, AVCodecContext *video_codec_context, AVFrame *frame) {
    const int res = av_hwframe_get_buffer(video_codec_context->hw_frames_ctx, frame, 0);
    if(res < 0) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: av_hwframe_get_buffer failed: %d", res);
        return false;
    }

    while(self->params.egl->glGetError()) {}

    AVVulkanDeviceContext *vv = video_codec_context_get_vulkan_data(video_codec_context);
    if(!vv) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: failed to get vulkan device context");
        return false;
    }

    if(!gsr_vk_funcs_load(&self->vk, vv->get_proc_addr, vv->inst, vv->act_dev))
        return false;

    self->vk_device = vv->act_dev;

    const bool is_p010 = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS;
    const VkFormat fmt_y  = is_p010 ? VK_FORMAT_R16_UNORM    : VK_FORMAT_R8_UNORM;
    const VkFormat fmt_uv = is_p010 ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
    const unsigned int gl_fmt_y  = is_p010 ? GL_R16  : GL_R8;
    const unsigned int gl_fmt_uv = is_p010 ? GL_RG16 : GL_RG8;

    if(!create_exportable_image(&self->vk, vv->act_dev, vv->phys_dev,
                                frame->width, frame->height, fmt_y,
                                &self->export_images[0], &self->export_memory[0], &self->export_memory_size[0]))
        return false;

    if(!create_exportable_image(&self->vk, vv->act_dev, vv->phys_dev,
                                frame->width / 2, frame->height / 2, fmt_uv,
                                &self->export_images[1], &self->export_memory[1], &self->export_memory_size[1]))
        return false;

    /* Export Vulkan memory as FDs and import into GL */
    for(int i = 0; i < 2; i++) {
        VkMemoryGetFdInfoKHR fd_info = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            .memory = self->export_memory[i],
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        };
        int fd = -1;
        if(self->vk.vkGetMemoryFdKHR(vv->act_dev, &fd_info, &fd) != VK_SUCCESS) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: vkGetMemoryFdKHR failed for plane %d", i);
            return false;
        }

        self->params.egl->glCreateMemoryObjectsEXT(1, &self->gl_memory_objects[i]);
        const int dedicated = 1;
        self->params.egl->glMemoryObjectParameterivEXT(self->gl_memory_objects[i],
                                                       GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
        self->params.egl->glImportMemoryFdEXT(self->gl_memory_objects[i], self->export_memory_size[i],
                                              GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
        if(!self->params.egl->glIsMemoryObjectEXT(self->gl_memory_objects[i])) {
            gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: failed to import memory FD for plane %d", i);
            return false;
        }
    }

    /* Create GL textures backed by the exportable images */
    self->params.egl->glGenTextures(2, self->target_textures);

    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->target_textures[0]);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
    self->params.egl->glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, gl_fmt_y,
                                           frame->width, frame->height,
                                           self->gl_memory_objects[0], 0);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);

    self->params.egl->glBindTexture(GL_TEXTURE_2D, self->target_textures[1]);
    self->params.egl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
    self->params.egl->glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, gl_fmt_uv,
                                           frame->width / 2, frame->height / 2,
                                           self->gl_memory_objects[1], 0);
    self->params.egl->glBindTexture(GL_TEXTURE_2D, 0);

    self->texture_sizes[0] = (vec2i){ frame->width,     frame->height     };
    self->texture_sizes[1] = (vec2i){ frame->width / 2, frame->height / 2 };

    /* Set up Vulkan command infrastructure */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = (uint32_t)get_graphics_queue_family(vv),
    };
    if(self->vk.vkCreateCommandPool(vv->act_dev, &pool_info, NULL, &self->command_pool) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: vkCreateCommandPool failed");
        return false;
    }

    VkCommandBufferAllocateInfo cb_alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = self->command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if(self->vk.vkAllocateCommandBuffers(vv->act_dev, &cb_alloc_info, &self->command_buffer) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: vkAllocateCommandBuffers failed");
        return false;
    }

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
    };
    if(self->vk.vkCreateFence(vv->act_dev, &fence_info, NULL, &self->fence) != VK_SUCCESS) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: vkCreateFence failed");
        return false;
    }

    /*
        The queues have to be retrieved with vkGetDeviceQueue2 because vkGetDeviceQueue only works for queues that
        were created without flags, and FFmpeg creates them with VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR
        when the driver supports it. vkGetDeviceQueue returns a NULL queue in that case, which crashes when it's used.
    */
    VkDeviceQueueInfo2 queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
        .flags = get_device_queue_create_flags(vv),
        .queueFamilyIndex = (uint32_t)get_graphics_queue_family(vv),
        .queueIndex = 0,
    };
    self->vk.vkGetDeviceQueue2(vv->act_dev, &queue_info, &self->vk_queue);
    if(!self->vk_queue) {
        gsr_log(GSR_LOG_LEVEL_ERROR, "gsr_video_encoder_vulkan_setup_textures: vkGetDeviceQueue2 failed");
        return false;
    }

    /* Transition export images UNDEFINED → GENERAL so GL can use them */
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    self->vk.vkBeginCommandBuffer(self->command_buffer, &begin_info);

    VkImageMemoryBarrier init_barriers[2];
    for(int i = 0; i < 2; i++) {
        init_barriers[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = 0,
            .dstAccessMask = 0,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = self->export_images[i],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
    }
    self->vk.vkCmdPipelineBarrier(self->command_buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, NULL, 0, NULL, 2, init_barriers);

    self->vk.vkEndCommandBuffer(self->command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &self->command_buffer,
    };
    self->vk.vkQueueSubmit(self->vk_queue, 1, &submit_info, self->fence);
    self->vk.vkWaitForFences(vv->act_dev, 1, &self->fence, VK_TRUE, UINT64_MAX);
    self->vk.vkResetFences(vv->act_dev, 1, &self->fence);
    self->vk.vkResetCommandBuffer(self->command_buffer, 0);

    /* Create GL→Vulkan sync semaphore (binary, exported via OPAQUE_FD) */
    if(self->params.egl->glGenSemaphoresEXT && self->params.egl->glImportSemaphoreFdEXT) {
        VkExportSemaphoreCreateInfo exp_sem_info = {
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
        };
        VkSemaphoreCreateInfo sem_info = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &exp_sem_info,
        };
        if(self->vk.vkCreateSemaphore(vv->act_dev, &sem_info, NULL, &self->gl_ready_semaphore) == VK_SUCCESS) {
            VkSemaphoreGetFdInfoKHR get_fd_info = {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
                .semaphore = self->gl_ready_semaphore,
                .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT,
            };
            int sem_fd = -1;
            if(self->vk.vkGetSemaphoreFdKHR(vv->act_dev, &get_fd_info, &sem_fd) == VK_SUCCESS) {
                self->params.egl->glGenSemaphoresEXT(1, &self->gl_semaphore);
                self->params.egl->glImportSemaphoreFdEXT(self->gl_semaphore, GL_HANDLE_TYPE_OPAQUE_FD_EXT, sem_fd);
            } else {
                self->vk.vkDestroySemaphore(vv->act_dev, self->gl_ready_semaphore, NULL);
                self->gl_ready_semaphore = VK_NULL_HANDLE;
            }
        }
    }

    return true;
}

static void gsr_video_encoder_vulkan_stop(gsr_video_encoder_vulkan *self, AVCodecContext *video_codec_context);

static bool gsr_video_encoder_vulkan_start(gsr_video_encoder *encoder, AVCodecContext *video_codec_context, AVFrame *frame) {
    gsr_video_encoder_vulkan *self = encoder->priv;

    if(self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_AMD && video_codec_context->codec_id == AV_CODEC_ID_HEVC) {
        video_codec_context->width = FFALIGN(video_codec_context->width, 2);
        video_codec_context->height = FFALIGN(video_codec_context->height, 2);
    } else if(self->params.egl->gpu_info.vendor == GSR_GPU_VENDOR_AMD && video_codec_context->codec_id == AV_CODEC_ID_AV1) {
        // TODO: Dont do this for VCN 5 and forward which should fix this hardware bug
        video_codec_context->width = FFALIGN(video_codec_context->width, 64);
        video_codec_context->height = FFALIGN(video_codec_context->height, 16);
    } else {
        video_codec_context->width = FFALIGN(video_codec_context->width, 2);
        video_codec_context->height = FFALIGN(video_codec_context->height, 2);
    }

    if(FFALIGN(video_codec_context->width, 2) != FFALIGN(frame->width, 2) || FFALIGN(video_codec_context->height, 2) != FFALIGN(frame->height, 2)) {
        gsr_log(GSR_LOG_LEVEL_WARNING, "gsr_video_encoder_vulkan_start: black bars have been added to the video because of a bug in AMD drivers/hardware. Record with h264/hevc vulkan codec instead (-k h264_vulkan) to get around this issue");
    }

    if(video_codec_context->width < 128)
        video_codec_context->width = 128;

    if(video_codec_context->height < 128)
        video_codec_context->height = 128;

    frame->width = video_codec_context->width;
    frame->height = video_codec_context->height;

    if(!gsr_video_encoder_vulkan_setup_context(self, video_codec_context)) {
        gsr_video_encoder_vulkan_stop(self, video_codec_context);
        return false;
    }

    if(!gsr_video_encoder_vulkan_setup_textures(self, video_codec_context, frame)) {
        gsr_video_encoder_vulkan_stop(self, video_codec_context);
        return false;
    }

    return true;
}

static void gsr_video_encoder_vulkan_copy_textures_to_frame(gsr_video_encoder *encoder, AVFrame *frame, gsr_color_conversion *color_conversion) {
    (void)color_conversion;
    gsr_video_encoder_vulkan *self = encoder->priv;
    AVVkFrame *vk_frame = (AVVkFrame*)frame->data[0];

    /* Wait for the previous frame's copy to finish before reusing the command buffer */
    // if(self->fence_submitted) {
    //     self->vk.vkWaitForFences(self->vk_device, 1, &self->fence, VK_TRUE, UINT64_MAX);
         self->vk.vkResetFences(self->vk_device, 1, &self->fence);
         self->fence_submitted = false;
    // }

    if(self->gl_ready_semaphore != VK_NULL_HANDLE && self->params.egl->glSignalSemaphoreEXT) {
        /* GPU-side GL→Vulkan sync: signal the semaphore from GL, then flush (no CPU stall) */
        unsigned int gl_textures[2] = { self->target_textures[0], self->target_textures[1] };
        unsigned int dst_layouts[2] = { GL_LAYOUT_GENERAL_EXT, GL_LAYOUT_GENERAL_EXT };
        self->params.egl->glSignalSemaphoreEXT(self->gl_semaphore, 0, NULL, 2, gl_textures, dst_layouts);
        self->params.egl->glFlush();
    } else {
        /* Fallback: CPU stall to ensure GL has finished */
        self->params.egl->glFinish();
    }

    self->vk.vkResetCommandBuffer(self->command_buffer, 0);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    self->vk.vkBeginCommandBuffer(self->command_buffer, &begin_info);

    /* Transition export images: GENERAL → TRANSFER_SRC_OPTIMAL */
    VkImageMemoryBarrier src_barriers[2];
    for(int i = 0; i < 2; i++) {
        src_barriers[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = self->export_images[i],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
    }
    self->vk.vkCmdPipelineBarrier(self->command_buffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, NULL, 0, NULL, 2, src_barriers);

    /*
     * Detect whether the encoder frame uses one multi-plane image (default NV12/P010
     * in FFmpeg) or two separate single-plane images.
     * Multi-plane: img[1] == VK_NULL_HANDLE or img[1] == img[0]
     */
    const bool multiplane = (vk_frame->img[1] == VK_NULL_HANDLE || vk_frame->img[1] == vk_frame->img[0]);

    if(multiplane) {
        /* Transition the encoder's multi-plane image to TRANSFER_DST_OPTIMAL */
        VkImageMemoryBarrier dst_barrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = vk_frame->layout[0],
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk_frame->img[0],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        self->vk.vkCmdPipelineBarrier(self->command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 1, &dst_barrier);

        /* Copy Y plane: export_images[0] (R8/R16) → encoder img PLANE_0 */
        VkImageCopy copy_y = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_PLANE_0_BIT, 0, 0, 1 },
            .extent = { (uint32_t)frame->width, (uint32_t)frame->height, 1 },
        };
        self->vk.vkCmdCopyImage(self->command_buffer,
            self->export_images[0], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk_frame->img[0],       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy_y);

        /* Copy UV plane: export_images[1] (RG8/RG16) → encoder img PLANE_1 */
        VkImageCopy copy_uv = {
            .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
            .dstSubresource = { VK_IMAGE_ASPECT_PLANE_1_BIT, 0, 0, 1 },
            .extent = { (uint32_t)frame->width / 2, (uint32_t)frame->height / 2, 1 },
        };
        self->vk.vkCmdCopyImage(self->command_buffer,
            self->export_images[1], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            vk_frame->img[0],       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copy_uv);

        /* Transition encoder image to GENERAL and update tracked layout */
        VkImageMemoryBarrier dst_barrier_back = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = vk_frame->img[0],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
        self->vk.vkCmdPipelineBarrier(self->command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, NULL, 0, NULL, 1, &dst_barrier_back);

        vk_frame->layout[0] = VK_IMAGE_LAYOUT_GENERAL;
        vk_frame->layout[1] = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        /* Two separate single-plane images */
        VkImageMemoryBarrier dst_barriers[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = vk_frame->layout[0],
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_frame->img[0],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = vk_frame->layout[1],
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_frame->img[1],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
        };
        self->vk.vkCmdPipelineBarrier(self->command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, NULL, 0, NULL, 2, dst_barriers);

        for(int i = 0; i < 2; i++) {
            VkImageCopy copy = {
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .extent = {
                    (uint32_t)(i == 0 ? frame->width     : frame->width / 2),
                    (uint32_t)(i == 0 ? frame->height    : frame->height / 2),
                    1
                },
            };
            self->vk.vkCmdCopyImage(self->command_buffer,
                self->export_images[i], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                vk_frame->img[i],       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copy);
        }

        VkImageMemoryBarrier dst_barriers_back[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_frame->img[0],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
            {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = vk_frame->img[1],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .levelCount = 1,
                    .layerCount = 1,
                },
            },
        };
        self->vk.vkCmdPipelineBarrier(self->command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0, 0, NULL, 0, NULL, 2, dst_barriers_back);

        vk_frame->layout[0] = VK_IMAGE_LAYOUT_GENERAL;
        vk_frame->layout[1] = VK_IMAGE_LAYOUT_GENERAL;
    }

    /* Transition export images back: TRANSFER_SRC_OPTIMAL → GENERAL for next GL frame */
    VkImageMemoryBarrier src_barriers_back[2];
    for(int i = 0; i < 2; i++) {
        src_barriers_back[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = self->export_images[i],
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },
        };
    }
    self->vk.vkCmdPipelineBarrier(self->command_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0, 0, NULL, 0, NULL, 2, src_barriers_back);

    self->vk.vkEndCommandBuffer(self->command_buffer);

    /*
     * Detect whether the encoder frame is multiplane to know how many timeline
     * semaphores need to be signaled.
     */
    const bool mp = (vk_frame->img[1] == VK_NULL_HANDLE || vk_frame->img[1] == vk_frame->img[0]);
    const int num_sems = mp ? 1 : 2;

    if(self->gl_ready_semaphore != VK_NULL_HANDLE) {
        /*
         * GPU-side sync path:
         *  - Wait on the GL binary semaphore before executing the copy.
         *  - Signal each AVVkFrame timeline semaphore so FFmpeg knows the frame is ready.
         */
        uint64_t signal_values[2];
        VkSemaphore signal_sems[2];
        for(int i = 0; i < num_sems; i++) {
            signal_values[i] = vk_frame->sem_value[i] + 1;
            signal_sems[i]   = vk_frame->sem[i];
        }

        VkTimelineSemaphoreSubmitInfo timeline_info = {
            .sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO,
            .signalSemaphoreValueCount = (uint32_t)num_sems,
            .pSignalSemaphoreValues    = signal_values,
        };

        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo submit_info = {
            .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext                = &timeline_info,
            .waitSemaphoreCount   = 1,
            .pWaitSemaphores      = &self->gl_ready_semaphore,
            .pWaitDstStageMask    = &wait_stage,
            .commandBufferCount   = 1,
            .pCommandBuffers      = &self->command_buffer,
            .signalSemaphoreCount = (uint32_t)num_sems,
            .pSignalSemaphores    = signal_sems,
        };
        self->vk.vkQueueSubmit(self->vk_queue, 1, &submit_info, self->fence);

        for(int i = 0; i < num_sems; i++)
            vk_frame->sem_value[i]++;
    } else {
        /* Fallback: plain submit, we already stalled via glFinish() */
        VkSubmitInfo submit_info = {
            .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers    = &self->command_buffer,
        };
        self->vk.vkQueueSubmit(self->vk_queue, 1, &submit_info, self->fence);
    }

    self->fence_submitted = true;
}

void gsr_video_encoder_vulkan_stop(gsr_video_encoder_vulkan *self, AVCodecContext *video_codec_context) {
    self->params.egl->glDeleteTextures(2, self->target_textures);
    self->target_textures[0] = 0;
    self->target_textures[1] = 0;

    if(self->vk_device) {
        /* Drain any in-flight copy before freeing resources */
        if(self->fence_submitted) {
            self->vk.vkWaitForFences(self->vk_device, 1, &self->fence, VK_TRUE, UINT64_MAX);
            self->fence_submitted = false;
        }
        if(self->gl_ready_semaphore)
            self->vk.vkDestroySemaphore(self->vk_device, self->gl_ready_semaphore, NULL);
        if(self->fence)
            self->vk.vkDestroyFence(self->vk_device, self->fence, NULL);
        /* Destroying the command pool also frees the command buffer */
        if(self->command_pool)
            self->vk.vkDestroyCommandPool(self->vk_device, self->command_pool, NULL);
        for(int i = 0; i < 2; i++) {
            if(self->export_images[i])
                self->vk.vkDestroyImage(self->vk_device, self->export_images[i], NULL);
            if(self->export_memory[i])
                self->vk.vkFreeMemory(self->vk_device, self->export_memory[i], NULL);
        }
    }

    if(video_codec_context && video_codec_context->hw_frames_ctx)
        av_buffer_unref(&video_codec_context->hw_frames_ctx);
    if(self->device_ctx)
        av_buffer_unref(&self->device_ctx);
}

static void gsr_video_encoder_vulkan_get_textures(gsr_video_encoder *encoder, unsigned int *textures, vec2i *texture_sizes, int *num_textures, gsr_destination_color *destination_color) {
    gsr_video_encoder_vulkan *self = encoder->priv;
    textures[0] = self->target_textures[0];
    textures[1] = self->target_textures[1];
    texture_sizes[0] = self->texture_sizes[0];
    texture_sizes[1] = self->texture_sizes[1];
    *num_textures = 2;
    *destination_color = self->params.color_depth == GSR_COLOR_DEPTH_10_BITS ? GSR_DESTINATION_COLOR_P010 : GSR_DESTINATION_COLOR_NV12;
}

static void gsr_video_encoder_vulkan_destroy(gsr_video_encoder *encoder, AVCodecContext *video_codec_context) {
    gsr_video_encoder_vulkan_stop(encoder->priv, video_codec_context);
    free(encoder->priv);
    free(encoder);
}

gsr_video_encoder* gsr_video_encoder_vulkan_create(const gsr_video_encoder_vulkan_params *params) {
    gsr_video_encoder *encoder = calloc(1, sizeof(gsr_video_encoder));
    if(!encoder)
        return NULL;

    gsr_video_encoder_vulkan *encoder_vulkan = calloc(1, sizeof(gsr_video_encoder_vulkan));
    if(!encoder_vulkan) {
        free(encoder);
        return NULL;
    }

    encoder_vulkan->params = *params;

    *encoder = (gsr_video_encoder) {
        .start = gsr_video_encoder_vulkan_start,
        .copy_textures_to_frame = gsr_video_encoder_vulkan_copy_textures_to_frame,
        .get_textures = gsr_video_encoder_vulkan_get_textures,
        .destroy = gsr_video_encoder_vulkan_destroy,
        .priv = encoder_vulkan
    };

    return encoder;
}
