/*
* Vulkan Example - Instanced mesh rendering, uses a separate vertex buffer for instanced data
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h> 
#include <vector>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"
#include "VulkanBuffer.hpp"
#include "VulkanTexture.hpp"
#include "VulkanModel.hpp"

#define VERTEX_BUFFER_BIND_ID   0
#define INSTANCE_BUFFER_BIND_ID 1
#define DESCRIPTOR_COUNT        4
#define ENABLE_VALIDATION       false
#define LIGHT_INTENSITY         70
#define INSTANCE_COUNT          2048
#define PLANET_SCALE            2.5f
#define LIGHT_SCALE             0.025f
#define CONSTRUCT_SCALE         24.0f
#define INSTANCE_SCALE          0.15f

struct {
    struct {
        VkImage image;
        VkImageView view;
        VkDeviceMemory memory;
    } color;
    struct {
        VkImage image;
        VkImageView view;
        VkDeviceMemory memory;
    } depth;
} multisampleTarget;

class VulkanExample : public VulkanExampleBase
{
public:
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    struct {
        vks::Texture2DArray rocksTex2DArr;
        vks::Texture2D planetTex2D;
        vks::Texture2D lightTex2D;
        vks::Texture2D constructTex2D;
    } textures;

    // Vertex layout for the models
    vks::VertexLayout vertexLayout = vks::VertexLayout({
        vks::VERTEX_COMPONENT_POSITION,
        vks::VERTEX_COMPONENT_NORMAL,
        vks::VERTEX_COMPONENT_UV,
        vks::VERTEX_COMPONENT_COLOR,
    });

    struct {
        vks::Model rockModel;
        vks::Model planetModel;
        vks::Model lightModel;
        vks::Model constructModel;
    } models;

    // Per-instance data block
    struct InstanceData {
        glm::vec3 pos;
        glm::vec3 rot;
        float scale;
        uint32_t texIndex;
    };
    // Contains the instanced data
    struct InstanceBuffer {
        VkBuffer buffer       = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        size_t size           = 0;
        VkDescriptorBufferInfo descriptor;
    } instanceBuffer;

    // M V P
    // M - MODEL MAT      - model space -> world space
    // V - VIEW MAT       - world space -> camera space
    // P - PROJECTION MAT - camera space -> square frustum space
    // MVP = P * V * M
    // gl_Position =  MVP * vec4(inPos, 1.0f);
    // someVector  =  MVP * vec4(inVec, 0.0f);
    struct UBOVS {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec4 lightPos = glm::vec4(0.707f*28.0f, -3.0f, -0.707f*28.0f, 1.0f);
        glm::vec4 camPos   = glm::vec4();
        float lightInt  = 0.0f;
        float locSpeed  = 0.0f;
        float globSpeed = 0.0f;
    } uboVS;

    struct {
        vks::Buffer scene;
    } uniformBuffers;

    VkPipelineLayout pipelineLayout;
    struct {
        VkPipeline instancedRocksVkPipeline;
        VkPipeline planetVkPipeline;
        VkPipeline lightVkPipeline;
        VkPipeline constructVkPipeline;
        VkPipeline starfieldVkPipeline;
    } pipelines;

    VkDescriptorSetLayout descriptorSetLayout;
    struct {
        VkDescriptorSet instancedRocksVkDescrSet;
        VkDescriptorSet planetVkDescrSet;
        VkDescriptorSet lightVkDescrSet;
        VkDescriptorSet constructVkDescrSet;
    } descriptorSets;

    VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
    {
        title = "Vulkan Example - Instanced mesh rendering - 229";
        enableTextOverlay = true;
        srand(time(NULL));
        cameraPos = { 15.2f, -8.5f, 0.0f };
        rotation = {-520.0f, -2925.0f, 0.0f };
        zoom = -48.0f;
        rotationSpeed = 0.25f;
    }

    ~VulkanExample()
    {
        vkDestroyPipeline(device, pipelines.instancedRocksVkPipeline, nullptr);
        vkDestroyPipeline(device, pipelines.planetVkPipeline, nullptr);
        vkDestroyPipeline(device, pipelines.lightVkPipeline, nullptr);
        vkDestroyPipeline(device, pipelines.constructVkPipeline, nullptr);
        vkDestroyPipeline(device, pipelines.starfieldVkPipeline, nullptr);

        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

        vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

        vkDestroyBuffer(device, instanceBuffer.buffer, nullptr);

        vkFreeMemory(device, instanceBuffer.memory, nullptr);

        models.rockModel.destroy();
        models.planetModel.destroy();
        models.lightModel.destroy();
        models.constructModel.destroy();

        textures.rocksTex2DArr.destroy();
        textures.planetTex2D.destroy();
        textures.lightTex2D.destroy();
        textures.constructTex2D.destroy();

        uniformBuffers.scene.destroy();
    }

    //////////////////////////
    /// BEGIN MSAA CONFIGURE
    //////////////////////////

    // Creates a multi sample render target (image and view) that is used to resolve
    // into the visible frame buffer target in the render pass
    void setupMultisampleTarget()
    {
        // Check if device supports requested sample count for color and depth frame buffer
        assert((deviceProperties.limits.framebufferColorSampleCounts >= sampleCount) && (deviceProperties.limits.framebufferDepthSampleCounts >= sampleCount));

        // Color target
        VkImageCreateInfo info = vks::initializers::imageCreateInfo();
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = swapChain.colorFormat;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.samples = sampleCount;
        // Image will only be used as a transient target
        info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget.color.image));

        VkMemoryRequirements memReqs;
        vkGetImageMemoryRequirements(device, multisampleTarget.color.image, &memReqs);
        VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;
        // We prefer a lazily allocated memory type
        // This means that the memory gets allocated when the implementation sees fit, e.g. when first using the images
        VkBool32 lazyMemTypePresent;
        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
        if (!lazyMemTypePresent)
        {
            // If this is not available, fall back to device local memory
            memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }
        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget.color.memory));
        vkBindImageMemory(device, multisampleTarget.color.image, multisampleTarget.color.memory, 0);

        // Create image view for the MSAA target
        VkImageViewCreateInfo viewInfo = vks::initializers::imageViewCreateInfo();
        viewInfo.image = multisampleTarget.color.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapChain.colorFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget.color.view));

        // Depth target
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = depthFormat;
        info.extent.width = width;
        info.extent.height = height;
        info.extent.depth = 1;
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.samples = sampleCount;
        // Image will only be used as a transient target
        info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VK_CHECK_RESULT(vkCreateImage(device, &info, nullptr, &multisampleTarget.depth.image));

        vkGetImageMemoryRequirements(device, multisampleTarget.depth.image, &memReqs);
        memAlloc = vks::initializers::memoryAllocateInfo();
        memAlloc.allocationSize = memReqs.size;

        memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT, &lazyMemTypePresent);
        if (!lazyMemTypePresent)
        {
            memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        }

        VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &multisampleTarget.depth.memory));
        vkBindImageMemory(device, multisampleTarget.depth.image, multisampleTarget.depth.memory, 0);

        // Create image view for the MSAA target
        viewInfo.image = multisampleTarget.depth.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        VK_CHECK_RESULT(vkCreateImageView(device, &viewInfo, nullptr, &multisampleTarget.depth.view));
    }

    // Setup a render pass for using a multi sampled attachment
    // and a resolve attachment that the msaa image is resolved
    // to at the end of the render pass
    void setupRenderPass() override
    {
        // Overrides the virtual function of the base class

        std::array<VkAttachmentDescription, 4> attachments = {};

        // Multisampled attachment that we render to
        attachments[0].format = swapChain.colorFormat;
        attachments[0].samples = sampleCount;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        // No longer required after resolve, this may save some bandwidth on certain GPUs
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        // This is the frame buffer attachment to where the multisampled image
        // will be resolved to and which will be presented to the swapchain
        attachments[1].format = swapChain.colorFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        // Multisampled depth attachment we render to
        attachments[2].format = depthFormat;
        attachments[2].samples = sampleCount;
        attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Depth resolve attachment
        attachments[3].format = depthFormat;
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference = {};
        depthReference.attachment = 2;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Resolve attachment reference for the color attachment
        VkAttachmentReference resolveReference = {};
        resolveReference.attachment = 1;
        resolveReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorReference;
        // Pass our resolve attachments to the sub pass
        subpass.pResolveAttachments = &resolveReference;
        subpass.pDepthStencilAttachment = &depthReference;

        std::array<VkSubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo = vks::initializers::renderPassCreateInfo();
        renderPassInfo.attachmentCount = attachments.size();
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 2;
        renderPassInfo.pDependencies = dependencies.data();

        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
    }

    // Frame buffer attachments must match with render pass setup,
    // so we need to adjust frame buffer creation to cover our
    // multisample target
    void setupFrameBuffer() override
    {
        // Overrides the virtual function of the base class

        std::array<VkImageView, 4> attachments;

        setupMultisampleTarget();

        attachments[0] = multisampleTarget.color.view;
        // attachment[1] = swapchain image
        attachments[2] = multisampleTarget.depth.view;
        attachments[3] = depthStencil.view;

        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.pNext = NULL;
        frameBufferCreateInfo.renderPass = renderPass;
        frameBufferCreateInfo.attachmentCount = attachments.size();
        frameBufferCreateInfo.pAttachments = attachments.data();
        frameBufferCreateInfo.width = width;
        frameBufferCreateInfo.height = height;
        frameBufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image
        frameBuffers.resize(swapChain.imageCount);
        for (uint32_t i = 0; i < frameBuffers.size(); i++)
        {
            attachments[1] = swapChain.buffers[i].view;
            VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
        }
    }

    //////////////////////////
    /// END MSAA CONFIGURE
    //////////////////////////

    void buildCommandBuffers() override
    {
        VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

        VkClearValue clearValues[3];
        clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
        clearValues[1].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
        clearValues[2].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
        renderPassBeginInfo.renderPass = renderPass;
        renderPassBeginInfo.renderArea.extent.width = width;
        renderPassBeginInfo.renderArea.extent.height = height;
        renderPassBeginInfo.clearValueCount = 3;
        renderPassBeginInfo.pClearValues = clearValues;

        for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
        {
            // Set target frame buffer
            renderPassBeginInfo.framebuffer = frameBuffers[i];

            VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

            vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
            vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

            VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
            vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

            VkDeviceSize offsets[1] = { 0 };

            // Star field
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planetVkDescrSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starfieldVkPipeline);
            vkCmdDraw(drawCmdBuffers[i], 4, 1, 0, 0);

            // Planet
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.planetVkDescrSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.planetVkPipeline);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.planetModel.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.planetModel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.planetModel.indexCount, 1, 0, 0, 0);

            // Light
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.lightVkDescrSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.lightVkPipeline);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.lightModel.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.lightModel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.lightModel.indexCount, 1, 0, 0, 0);

            // Construct
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.constructVkDescrSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.constructVkPipeline);
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.constructModel.vertices.buffer, offsets);
            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.constructModel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(drawCmdBuffers[i], models.constructModel.indexCount, 1, 0, 0, 0);

            // Instanced rocks
            vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets.instancedRocksVkDescrSet, 0, NULL);
            vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.instancedRocksVkPipeline);
            // Binding point 0 : Mesh vertex buffer
            vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &models.rockModel.vertices.buffer, offsets);
            // Binding point 1 : Instance data buffer
            vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

            vkCmdBindIndexBuffer(drawCmdBuffers[i], models.rockModel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

            // Render instances
            vkCmdDrawIndexed(drawCmdBuffers[i], models.rockModel.indexCount, INSTANCE_COUNT, 0, 0, 0);

            vkCmdEndRenderPass(drawCmdBuffers[i]);

            VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
        }
    }

    void loadAssets()
    {
        models.rockModel.loadFromFile(getAssetPath()   + "models/rock01.dae",             vertexLayout, INSTANCE_SCALE, vulkanDevice, queue);
        models.planetModel.loadFromFile(getAssetPath() + "models/sphere_nonideal.obj",    vertexLayout, PLANET_SCALE,   vulkanDevice, queue);
        models.lightModel.loadFromFile(getAssetPath()  + "models/sphere.obj",             vertexLayout, LIGHT_SCALE,    vulkanDevice, queue);
        models.constructModel.loadFromFile(getAssetPath()  + "models/cage_construct.obj", vertexLayout, CONSTRUCT_SCALE,    vulkanDevice, queue);

        // Textures
        std::string texFormatSuffix;
        VkFormat texFormat;
        // Get supported compressed texture format
        if (vulkanDevice->features.textureCompressionBC) {
            texFormatSuffix = "_bc3_unorm";
            texFormat = VK_FORMAT_BC3_UNORM_BLOCK;
        }
        else if (vulkanDevice->features.textureCompressionASTC_LDR) {
            texFormatSuffix = "_astc_8x8_unorm";
            texFormat = VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
        }
        else if (vulkanDevice->features.textureCompressionETC2) {
            texFormatSuffix = "_etc2_unorm";
            texFormat = VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        }
        else {
            vks::tools::exitFatal("Device does not support any compressed texture format!", "Error");
        }

        textures.rocksTex2DArr.loadFromFile(getAssetPath() + "textures/texturearray_rocks" + texFormatSuffix + ".ktx", texFormat, vulkanDevice, queue);
        textures.planetTex2D.loadFromFile(getAssetPath()   + "textures/lava_from_gimp_planet_bc3_unorm.dds", VK_FORMAT_BC3_UNORM_BLOCK, vulkanDevice, queue);
        textures.lightTex2D.loadFromFile(getAssetPath()    + "textures/lava_from_gimp_light_bc3_unorm.dds", VK_FORMAT_BC3_UNORM_BLOCK, vulkanDevice, queue);
        textures.constructTex2D.loadFromFile(getAssetPath()    + "textures/lava_from_gimp_planet_bc3_unorm.dds", VK_FORMAT_BC3_UNORM_BLOCK, vulkanDevice, queue);
    }

    void setupDescriptorPool()
    {
        // Example uses one ubo
        std::vector<VkDescriptorPoolSize> poolSizes =
        {
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTOR_COUNT),
            vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, DESCRIPTOR_COUNT),
        };

        VkDescriptorPoolCreateInfo descriptorPoolInfo =
            vks::initializers::descriptorPoolCreateInfo(
                poolSizes.size(),
                poolSizes.data(),
                DESCRIPTOR_COUNT);

        VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
    }

    void setupDescriptorSetLayout()
    {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
        {
            // Binding 0 : Vertex shader uniform buffer
            vks::initializers::descriptorSetLayoutBinding(
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                VK_SHADER_STAGE_VERTEX_BIT,
                0),
            // Binding 1 : Fragment shader combined sampler
            vks::initializers::descriptorSetLayoutBinding(
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                VK_SHADER_STAGE_FRAGMENT_BIT,
                1),
        };

        VkDescriptorSetLayoutCreateInfo descriptorLayout =
            vks::initializers::descriptorSetLayoutCreateInfo(
                setLayoutBindings.data(),
                setLayoutBindings.size());

        VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

        VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
            vks::initializers::pipelineLayoutCreateInfo(
                &descriptorSetLayout,
                1);

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
    }

    void setupDescriptorSet()
    {
        VkDescriptorSetAllocateInfo descripotrSetAllocInfo;
        std::vector<VkWriteDescriptorSet> writeDescriptorSets;

        descripotrSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);;

        // Instanced rocks
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.instancedRocksVkDescrSet));
        writeDescriptorSets = {
            vks::initializers::writeDescriptorSet(descriptorSets.instancedRocksVkDescrSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),	// Binding 0 : Vertex shader uniform buffer
            vks::initializers::writeDescriptorSet(descriptorSets.instancedRocksVkDescrSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.rocksTex2DArr.descriptor)	// Binding 1 : Color map
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Planet
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.planetVkDescrSet));
        writeDescriptorSets = {
            vks::initializers::writeDescriptorSet(descriptorSets.planetVkDescrSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),			// Binding 0 : Vertex shader uniform buffer
            vks::initializers::writeDescriptorSet(descriptorSets.planetVkDescrSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.planetTex2D.descriptor)			// Binding 1 : Color map
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Light
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.lightVkDescrSet));
        writeDescriptorSets = {
            vks::initializers::writeDescriptorSet(descriptorSets.lightVkDescrSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),			// Binding 0 : Vertex shader uniform buffer
            vks::initializers::writeDescriptorSet(descriptorSets.lightVkDescrSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.lightTex2D.descriptor)			// Binding 1 : Color map
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

        // Construct descriptor sets
        VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descripotrSetAllocInfo, &descriptorSets.constructVkDescrSet));
        writeDescriptorSets = {
            vks::initializers::writeDescriptorSet(descriptorSets.constructVkDescrSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,	0, &uniformBuffers.scene.descriptor),			// Binding 0 : Vertex shader uniform buffer
            vks::initializers::writeDescriptorSet(descriptorSets.constructVkDescrSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &textures.constructTex2D.descriptor)			// Binding 1 : Color map
        };
        vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

    }

    void preparePipelines()
    {
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
            vks::initializers::pipelineInputAssemblyStateCreateInfo(
                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                0,
                VK_FALSE);

        VkPipelineRasterizationStateCreateInfo rasterizationState =
            vks::initializers::pipelineRasterizationStateCreateInfo(
                VK_POLYGON_MODE_FILL,
                VK_CULL_MODE_BACK_BIT,
                VK_FRONT_FACE_CLOCKWISE,
                0);

        VkPipelineColorBlendAttachmentState blendAttachmentState =
            vks::initializers::pipelineColorBlendAttachmentState(
                0xf,
                VK_FALSE);

        VkPipelineColorBlendStateCreateInfo colorBlendState =
            vks::initializers::pipelineColorBlendStateCreateInfo(
                1,
                &blendAttachmentState);

        VkPipelineDepthStencilStateCreateInfo depthStencilState =
            vks::initializers::pipelineDepthStencilStateCreateInfo(
                VK_TRUE,
                VK_TRUE,
                VK_COMPARE_OP_LESS_OR_EQUAL);

        VkPipelineViewportStateCreateInfo viewportState =
            vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

        VkPipelineMultisampleStateCreateInfo multisampleState{};
        multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleState.rasterizationSamples = sampleCount;		// Number of samples to use for rasterization
        multisampleState.sampleShadingEnable = VK_TRUE;				// Enable per-sample shading (instead of per-fragment)
        multisampleState.minSampleShading = 0.25f;					// Minimum fraction for sample shading


        std::vector<VkDynamicState> dynamicStateEnables = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };
        VkPipelineDynamicStateCreateInfo dynamicState =
            vks::initializers::pipelineDynamicStateCreateInfo(
                dynamicStateEnables.data(),
                dynamicStateEnables.size(),
                0);

        // Load shaders
        std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo =
            vks::initializers::pipelineCreateInfo(
                pipelineLayout,
                renderPass,
                0);

        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
        pipelineCreateInfo.pRasterizationState = &rasterizationState;
        pipelineCreateInfo.pColorBlendState = &colorBlendState;
        pipelineCreateInfo.pMultisampleState = &multisampleState;
        pipelineCreateInfo.pViewportState = &viewportState;
        pipelineCreateInfo.pDepthStencilState = &depthStencilState;
        pipelineCreateInfo.pDynamicState = &dynamicState;
        pipelineCreateInfo.stageCount = shaderStages.size();
        pipelineCreateInfo.pStages = shaderStages.data();

        // This example uses two different input states, one for the instanced part and one for non-instanced rendering
        VkPipelineVertexInputStateCreateInfo inputState = vks::initializers::pipelineVertexInputStateCreateInfo();
        std::vector<VkVertexInputBindingDescription> bindingDescriptions;
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

        // Vertex input bindings
        // The instancing pipeline uses a vertex input state with two bindings
        bindingDescriptions = {
            // Binding point 0: Mesh vertex layout description at per-vertex rate
            vks::initializers::vertexInputBindingDescription(VERTEX_BUFFER_BIND_ID, vertexLayout.stride(), VK_VERTEX_INPUT_RATE_VERTEX),
            // Binding point 1: Instanced data at per-instance rate
            vks::initializers::vertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE)
        };

        // Vertex attribute bindings
        // Note that the shader declaration for per-vertex and per-instance attributes is the same, the different input rates are only stored in the bindings:
        // instanced.vert:
        //	layout (location = 0) in vec3 inPos;			Per-Vertex
        //	...
        //	layout (location = 4) in vec3 instancePos;	Per-Instance
        attributeDescriptions = {
            // Per-vertex attributees
            // These are advanced for each vertex fetched by the vertex shader
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 0, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 0: Position
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 1, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Location 1: Normal
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 2, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6),		// Location 2: Texture coordinates
            vks::initializers::vertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID, 3, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 8),	// Location 3: Color
            // Per-Instance attributes
            // These are fetched for each instance rendered
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 5, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3),	// Location 4: Position
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 4, VK_FORMAT_R32G32B32_SFLOAT, 0),					// Location 5: Rotation
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 6, VK_FORMAT_R32_SFLOAT,sizeof(float) * 6),			// Location 6: Scale
            vks::initializers::vertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID, 7, VK_FORMAT_R32_SINT, sizeof(float) * 7),			// Location 7: Texture array layer index
        };
        inputState.pVertexBindingDescriptions = bindingDescriptions.data();
        inputState.pVertexAttributeDescriptions = attributeDescriptions.data();

        pipelineCreateInfo.pVertexInputState = &inputState;

        // Instancing pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing-229/instancing.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing-229/instancing.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // Use all input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
        inputState.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.instancedRocksVkPipeline));

        // Planet rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing-229/planet.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing-229/planet.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // Only use the non-instanced input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = 1;
        inputState.vertexAttributeDescriptionCount = 4;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.planetVkPipeline));

        // Light rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing-229/light.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing-229/light.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // Only use the non-instanced input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = 1;
        inputState.vertexAttributeDescriptionCount = 4;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.lightVkPipeline));

        // Construct rendering pipeline
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing-229/construct.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing-229/construct.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // Only use the non-instanced input bindings and attribute descriptions
        inputState.vertexBindingDescriptionCount = 1;
        inputState.vertexAttributeDescriptionCount = 4;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.constructVkPipeline));

        // Star field pipeline
        rasterizationState.cullMode = VK_CULL_MODE_NONE;
        depthStencilState.depthWriteEnable = VK_FALSE;
        shaderStages[0] = loadShader(getAssetPath() + "shaders/instancing-229/starfield.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
        shaderStages[1] = loadShader(getAssetPath() + "shaders/instancing-229/starfield.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
        // Vertices are generated in the vertex shader
        inputState.vertexBindingDescriptionCount = 0;
        inputState.vertexAttributeDescriptionCount = 0;
        VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.starfieldVkPipeline));
    }

    float rnd(float range)
    {
        return range * (rand() / double(RAND_MAX));
    }

    void prepareInstanceData()
    {
        std::vector<InstanceData> instanceData;
        instanceData.resize(INSTANCE_COUNT);

        std::mt19937 rndGenerator(time(NULL));
        std::uniform_real_distribution<float> uniformDist(0.0, 1.0);

        // Distribute rocks randomly on two different rings

        std::vector<glm::vec2> rings = {
            {   5.0f,   7.0f },
            {   8.0f,  11.0f },
            {  13.0f,  17.0f },
            {  20.0f,  26.0f },
            {  30.0f,  40.0f },
            {  48.0f,  60.0f }
        };
        const auto numOfChunks = rings.size();
        const auto numInChunk  = INSTANCE_COUNT / rings.size();
        float rho, theta;

        for (auto instIdInChunk = 0; instIdInChunk < numInChunk; instIdInChunk++)
        {
            for (auto ringId = 0; ringId < numOfChunks; ringId++)
            {
                const auto instanceId    = instIdInChunk + ringId*numInChunk;
                auto& currentInstanceRef = instanceData[instanceId];

                rho   = sqrt((pow(rings.at(ringId)[1], 2.0f) - pow(rings.at(ringId)[0], 2.0f)) * uniformDist(rndGenerator) + pow(rings.at(ringId)[0], 2.0f));
                theta = 2.0 * M_PI * uniformDist(rndGenerator);

                currentInstanceRef.pos      = glm::vec3(rho*cos(theta), uniformDist(rndGenerator) * 0.05f - 0.25f, rho*sin(theta));
                currentInstanceRef.rot      = glm::vec3(M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator), M_PI * uniformDist(rndGenerator));
                currentInstanceRef.scale    = 1.5f + uniformDist(rndGenerator) - uniformDist(rndGenerator);
                currentInstanceRef.texIndex = rnd(textures.rocksTex2DArr.layerCount);
                currentInstanceRef.scale    *= 0.75f;
            }
        }

        instanceBuffer.size = instanceData.size() * sizeof(InstanceData);

        // Staging
        // Instanced data is static, copy to device local memory
        // This results in better performance

        struct {
            VkDeviceMemory memory;
            VkBuffer buffer;
        } stagingBuffer;

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            instanceBuffer.size,
            &stagingBuffer.buffer,
            &stagingBuffer.memory,
            instanceData.data()));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            instanceBuffer.size,
            &instanceBuffer.buffer,
            &instanceBuffer.memory));

        // Copy to staging buffer
        VkCommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

        VkBufferCopy copyRegion = { };
        copyRegion.size = instanceBuffer.size;
        vkCmdCopyBuffer(
            copyCmd,
            stagingBuffer.buffer,
            instanceBuffer.buffer,
            1,
            &copyRegion);

        VulkanExampleBase::flushCommandBuffer(copyCmd, queue, true);

        instanceBuffer.descriptor.range = instanceBuffer.size;
        instanceBuffer.descriptor.buffer = instanceBuffer.buffer;
        instanceBuffer.descriptor.offset = 0;

        // Destroy staging resources
        vkDestroyBuffer(device, stagingBuffer.buffer, nullptr);
        vkFreeMemory(device, stagingBuffer.memory, nullptr);
    }

    void prepareUniformBuffers()
    {
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &uniformBuffers.scene,
            sizeof(uboVS)));

        // Map persistent
        VK_CHECK_RESULT(uniformBuffers.scene.map());

        updateUniformBuffer(true);
    }

    void updateLight()
    {
        static float     G  = 2.5f;
        static float     mi = 10.0f;
        static float     mp = 100.0f;
        static glm::vec3 pi = { 45.0f, 0.0f, 10.0f };
        static glm::vec3 pp = { 0.0f,  0.0f, 0.0f };
        static glm::vec3 vi = { -1.0f, -0.3f, 1.0f };
        static glm::vec3 ai = { 0.0f,  0.0f, 0.0f };
        static glm::vec3 fi = { 0.0f,  0.0f, 0.0f };

        float r = glm::distance(pi, pp);
        glm::vec3 dirVec = glm::normalize(pi - pp);
        fi = -dirVec * G*mp*mi/(r*r);
        ai = fi/mi;
        vi = vi + ai*frameTimer;
        pi = pi + vi*frameTimer;

        const float k = 0.25f * frameTimer;
        uboVS.lightInt = LIGHT_INTENSITY*k + uboVS.lightInt*(1.0f - k);
        uboVS.lightPos = glm::vec4(pi, 1.0f);
    }

    void updateUniformBuffer(bool viewChanged)
    {
        if (viewChanged)
        {
//            std::cout << "  >> VulkanExample-229::updateUniformBuffer(bool viewChanged) cameraPos = {" << cameraPos.x << " , " << cameraPos.y << " , " << cameraPos.z << "}\n";
//            std::cout << "  >> VulkanExample-229::updateUniformBuffer(bool viewChanged) rotation = {" << rotation.x << ",  " << rotation.y << " , " << rotation.z << "}\n";
//            std::cout << "  >> VulkanExample-229::updateUniformBuffer(bool viewChanged) zoom = {" << zoom << "}\n";

            uboVS.projection = glm::perspective(glm::radians(60.0f), (float)width / (float)height, 0.1f, 256.0f);

            uboVS.view = glm::translate(glm::mat4(), glm::vec3(0.0f, 0.0f, zoom)) * glm::translate(glm::mat4(), cameraPos);
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.x/16), glm::vec3(1.0f, 0.0f, 0.0f));
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.y/16), glm::vec3(0.0f, 1.0f, 0.0f));
            uboVS.view = glm::rotate(uboVS.view, glm::radians(rotation.z/16), glm::vec3(0.0f, 0.0f, 1.0f));

            // Computing REAL camera coordinates, with rotation, zoom, etc... from MV matrix.
            glm::mat3 rotMat(uboVS.view);
            glm::vec3 d(uboVS.view[3]);
            uboVS.camPos = glm::vec4(-d * rotMat, 1.0f);
        }

        if (!paused)
        {
            uboVS.locSpeed  += frameTimer * 0.35f;
            uboVS.globSpeed += frameTimer * 0.01f;
            updateLight();
        }
        memcpy(uniformBuffers.scene.mapped, &uboVS, sizeof(uboVS));
    }

    void draw()
    {
        VulkanExampleBase::prepareFrame();

        // Command buffer to be sumitted to the queue
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

        // Submit to queue
        VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

        VulkanExampleBase::submitFrame();
    }

    // Returns the maximum sample count usable by the platform
    VkSampleCountFlagBits getMaxUsableSampleCount()
    {
        VkSampleCountFlags counts = std::min(deviceProperties.limits.framebufferColorSampleCounts,
                                            deviceProperties.limits.framebufferDepthSampleCounts);

        if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
        if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
        if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
        if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; } // For this I have support on Intel HD4000.
        if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
        if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }
        return VK_SAMPLE_COUNT_1_BIT;
    }

    void prepare() override
    {
        sampleCount = getMaxUsableSampleCount();
        VulkanExampleBase::prepare();
        loadAssets();
        prepareInstanceData();
        prepareUniformBuffers();
        setupDescriptorSetLayout();
        preparePipelines();
        setupDescriptorPool();
        setupDescriptorSet();
        buildCommandBuffers();
        prepared = true;
    }

    virtual void render() override
    {
        // Fix unstability when freezing runtime or low fps.
        if (frameTimer > 0.1f) frameTimer = 0.1f;

        if (!prepared)
        {
            return;
        }
        draw();
        if (!paused)
        {
            updateUniformBuffer(false);
        }
    }

    virtual void viewChanged() override
    {
        updateUniformBuffer(true);
    }

    virtual void getOverlayText(VulkanTextOverlay *textOverlay) override
    {
        textOverlay->addText("Rendering " + std::to_string(INSTANCE_COUNT) + " instances", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
    }
};

VULKAN_EXAMPLE_MAIN()
