#pragma once
#include "ubo_buffer.h"
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <stdexcept>
#include <utility>

namespace BigHero::Render
{
    /// 管理描述符集布局、池、集合
    /// set0: 相机UBO；set1: 光照UBO(binding0) + 漫反射纹理采样(binding1)
    /// 支持多组分配（每组对应一个帧并行槽位的相机/光照描述符）
    class DescriptorManager
    {
    public:
        VkDevice device = VK_NULL_HANDLE;
        VkDescriptorSetLayout layoutCamera = VK_NULL_HANDLE;
        VkDescriptorSetLayout layoutLight = VK_NULL_HANDLE;
        VkDescriptorSetLayout layoutCubeShadow = VK_NULL_HANDLE;
        // 延迟渲染：GBuffer 输入附件描述符布局（set2，3 张输入附件）
        VkDescriptorSetLayout layoutGBufferInput = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;
        // 每交换链图像的 GBuffer 输入附件集合（延迟光照子通道采样用）
        std::vector<VkDescriptorSet> gbufferSets;

        DescriptorManager() = default;

        // 延迟初始化，必须传入合法VkDevice后再创建资源
        void Init(VkDevice dev)
        {
            device = dev;
            if (device == VK_NULL_HANDLE)
                throw std::runtime_error("DescriptorManager::Init: VkDevice为空！请先初始化设备");
            CreateLayouts();
            CreateDescriptorPool();
        }

        // 禁止拷贝，资源独占
        DescriptorManager(const DescriptorManager&) = delete;
        DescriptorManager& operator=(const DescriptorManager&) = delete;

        // 移动语义
        DescriptorManager(DescriptorManager&& other) noexcept
        {
            Swap(other);
        }
        DescriptorManager& operator=(DescriptorManager&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                Swap(other);
            }
            return *this;
        }

        ~DescriptorManager()
        {
            Release();
        }

        /// 分配一组描述符集（1套相机+1套光照）
        void AllocateSet()
        {
            AllocateSets(1);
        }

        /// 分配groups组描述符集，顺序为[相机0,光照0,立方体阴影0,相机1,光照1,立方体阴影1,...]
        /// 每组含 3 个集合：set0 相机UBO、set1 光照/纹理、set2 立方体阴影矩阵UBO
        void AllocateSets(uint32_t groups)
        {
            if (groups == 0)
                return;

            std::vector<VkDescriptorSetLayout> layouts = { layoutCamera, layoutLight, layoutCubeShadow };
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
            allocInfo.pSetLayouts = layouts.data();

            for (uint32_t g = 0; g < groups; ++g)
            {
                VkDescriptorSet sets[3];
                const VkResult res = vkAllocateDescriptorSets(device, &allocInfo, sets);
                if (res != VK_SUCCESS)
                    throw std::runtime_error("DescriptorManager: 分配描述符集失败");

                descriptorSets.push_back(sets[0]);
                descriptorSets.push_back(sets[1]);
                descriptorSets.push_back(sets[2]);
            }
        }

        /// 更新指定索引的描述符集，绑定UBO缓冲
        template<typename T>
        void UpdateSet(uint32_t setIndex, uint32_t binding, const UboBuffer<T>& uboBuf)
        {
            if (!uboBuf.IsValid() || setIndex >= descriptorSets.size())
                return;

            VkDescriptorBufferInfo bufInfo{};
            bufInfo.buffer = uboBuf.buffer;
            bufInfo.offset = 0;
            bufInfo.range = GetUboByteSize<T>();

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets[setIndex];
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufInfo;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        /// 更新指定索引的描述符集，绑定合并图像采样器（漫反射纹理）
        void UpdateSetImage(uint32_t setIndex, uint32_t binding,
            VkImageView view, VkSampler sampler,
            VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            if (setIndex >= descriptorSets.size())
                return;

            VkDescriptorImageInfo imageInfo{};
            imageInfo.sampler = sampler;
            imageInfo.imageView = view;
            imageInfo.imageLayout = layout;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = descriptorSets[setIndex];
            write.dstBinding = binding;
            write.dstArrayElement = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.descriptorCount = 1;
            write.pImageInfo = &imageInfo;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        /// 释放全部Vulkan资源
        void Release() noexcept
        {
            if (device == VK_NULL_HANDLE) return;

            if (!descriptorSets.empty())
                vkFreeDescriptorSets(device, descriptorPool, static_cast<uint32_t>(descriptorSets.size()), descriptorSets.data());
            descriptorSets.clear();

            if (descriptorPool != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                descriptorPool = VK_NULL_HANDLE;
            }
            if (layoutCamera != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, layoutCamera, nullptr);
                layoutCamera = VK_NULL_HANDLE;
            }
            if (layoutLight != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, layoutLight, nullptr);
                layoutLight = VK_NULL_HANDLE;
            }
            if (layoutCubeShadow != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, layoutCubeShadow, nullptr);
                layoutCubeShadow = VK_NULL_HANDLE;
            }
            if (layoutGBufferInput != VK_NULL_HANDLE)
            {
                vkDestroyDescriptorSetLayout(device, layoutGBufferInput, nullptr);
                layoutGBufferInput = VK_NULL_HANDLE;
            }
            device = VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return device != VK_NULL_HANDLE && layoutCamera != VK_NULL_HANDLE && layoutLight != VK_NULL_HANDLE && layoutCubeShadow != VK_NULL_HANDLE && layoutGBufferInput != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE;
        }

        [[nodiscard]] const std::vector<VkDescriptorSet>& GetSets() const noexcept
        {
            return descriptorSets;
        }

        [[nodiscard]] const std::vector<VkDescriptorSet>& GetGBufferSets() const noexcept
        {
            return gbufferSets;
        }

        /// 分配 count 组 GBuffer 输入附件描述符集（每交换链图像一组）
        void AllocateGBufferSets(uint32_t count)
        {
            if (count == 0 || layoutGBufferInput == VK_NULL_HANDLE)
                return;
            gbufferSets.clear();
            gbufferSets.reserve(count);
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &layoutGBufferInput;
            for (uint32_t i = 0; i < count; ++i)
            {
                VkDescriptorSet set = VK_NULL_HANDLE;
                const VkResult res = vkAllocateDescriptorSets(device, &allocInfo, &set);
                if (res != VK_SUCCESS)
                    throw std::runtime_error("DescriptorManager: 分配GBuffer描述符集失败");
                gbufferSets.push_back(set);
            }
        }

        /// 更新指定索引的 GBuffer 输入附件集（绑定 3 张 GBuffer 图像视图）
        void UpdateGBufferSet(uint32_t index, VkImageView albedo, VkImageView normal,
            VkImageView position)
        {
            if (index >= gbufferSets.size())
                return;
            const VkDescriptorImageInfo infos[3] = {
                { VK_NULL_HANDLE, albedo, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { VK_NULL_HANDLE, normal, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
                { VK_NULL_HANDLE, position, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL }
            };
            std::array<VkWriteDescriptorSet, 3> writes{};
            for (uint32_t b = 0; b < 3; ++b)
            {
                writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[b].dstSet = gbufferSets[index];
                writes[b].dstBinding = b;
                writes[b].dstArrayElement = 0;
                writes[b].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                writes[b].descriptorCount = 1;
                writes[b].pImageInfo = &infos[b];
            }
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        }

    private:
        void Swap(DescriptorManager& other) noexcept
        {
            std::swap(device, other.device);
            std::swap(layoutCamera, other.layoutCamera);
            std::swap(layoutLight, other.layoutLight);
            std::swap(layoutCubeShadow, other.layoutCubeShadow);
            std::swap(descriptorPool, other.descriptorPool);
            std::swap(descriptorSets, other.descriptorSets);
        }

        /// 创建两套描述符布局，匹配着色器set/binding
        void CreateLayouts()
        {
            if (device == VK_NULL_HANDLE)
            {
                throw std::runtime_error("DescriptorManager::CreateLayouts: VkDevice为空！请先初始化设备");
            }

            // set=0 binding=0 : CameraUBO（顶点阶段）
            VkDescriptorSetLayoutBinding camBinding{};
            camBinding.binding = 0;
            camBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            camBinding.descriptorCount = 1;
            camBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            camBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo camLayoutInfo{};
            camLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            camLayoutInfo.bindingCount = 1;
            camLayoutInfo.pBindings = &camBinding;
            VkResult res = vkCreateDescriptorSetLayout(device, &camLayoutInfo, nullptr, &layoutCamera);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: 创建相机集布局失败");

            // set=1 binding=0 : LightUBO（片段阶段）
            // set=1 binding=1 : 反照率纹理合并采样器（片段阶段）
            // set=1 binding=2 : 法线贴图合并采样器（片段阶段）
            // set=1 binding=3 : 阴影贴图合并采样器（片段阶段）
            // set=1 binding=4 : 环境立方图（天空盒/IBL源，片段阶段）
            // set=1 binding=5 : 辐照度立方图（IBL漫反射，片段阶段）
            // set=1 binding=6 : 预滤波立方图（IBL镜面，片段阶段）
            // set=1 binding=7 : BRDF LUT（IBL分裂求和，片段阶段）
            // set=1 binding=8 : 点光源立方体阴影贴图（片段阶段）
            std::array<VkDescriptorSetLayoutBinding, 9> lightBindings{};
            lightBindings[0].binding = 0;
            lightBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            lightBindings[0].descriptorCount = 1;
            lightBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBindings[0].pImmutableSamplers = nullptr;
            lightBindings[1].binding = 1;
            lightBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lightBindings[1].descriptorCount = 1;
            lightBindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBindings[1].pImmutableSamplers = nullptr;
            lightBindings[2].binding = 2;
            lightBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lightBindings[2].descriptorCount = 1;
            lightBindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBindings[2].pImmutableSamplers = nullptr;
            lightBindings[3].binding = 3;
            lightBindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lightBindings[3].descriptorCount = 1;
            lightBindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBindings[3].pImmutableSamplers = nullptr;
            for (uint32_t b = 4; b < 8; ++b)
            {
                lightBindings[b].binding = b;
                lightBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                lightBindings[b].descriptorCount = 1;
                lightBindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                lightBindings[b].pImmutableSamplers = nullptr;
            }
            lightBindings[8].binding = 8;
            lightBindings[8].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            lightBindings[8].descriptorCount = 1;
            lightBindings[8].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBindings[8].pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
            lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lightLayoutInfo.bindingCount = static_cast<uint32_t>(lightBindings.size());
            lightLayoutInfo.pBindings = lightBindings.data();
            res = vkCreateDescriptorSetLayout(device, &lightLayoutInfo, nullptr, &layoutLight);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: 创建光照集布局失败");

            // set=2 binding=0 : PointShadowUBO（6 个面视投影矩阵，顶点阶段）
            VkDescriptorSetLayoutBinding cubeShadowBinding{};
            cubeShadowBinding.binding = 0;
            cubeShadowBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            cubeShadowBinding.descriptorCount = 1;
            cubeShadowBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            cubeShadowBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo cubeShadowLayoutInfo{};
            cubeShadowLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            cubeShadowLayoutInfo.bindingCount = 1;
            cubeShadowLayoutInfo.pBindings = &cubeShadowBinding;
            res = vkCreateDescriptorSetLayout(device, &cubeShadowLayoutInfo, nullptr, &layoutCubeShadow);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: 创建立方体阴影集布局失败");

            // set=2 (延迟光照子通道): 3 张 GBuffer 输入附件（albedo/normal/position）
            std::array<VkDescriptorSetLayoutBinding, 3> gbufferBindings{};
            for (uint32_t b = 0; b < 3; ++b)
            {
                gbufferBindings[b].binding = b;
                gbufferBindings[b].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
                gbufferBindings[b].descriptorCount = 1;
                gbufferBindings[b].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                gbufferBindings[b].pImmutableSamplers = nullptr;
            }
            VkDescriptorSetLayoutCreateInfo gbufferLayoutInfo{};
            gbufferLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            gbufferLayoutInfo.bindingCount = static_cast<uint32_t>(gbufferBindings.size());
            gbufferLayoutInfo.pBindings = gbufferBindings.data();
            res = vkCreateDescriptorSetLayout(device, &gbufferLayoutInfo, nullptr, &layoutGBufferInput);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: 创建GBuffer输入附件布局失败");
        }

        /// 创建描述符池，预留UBO、合并采样器与输入附件容量
        void CreateDescriptorPool()
        {
            std::vector<VkDescriptorPoolSize> poolSizes = {
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 200 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 200 },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 50 }
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = 350;

            const VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: 创建描述符池失败");
        }
    };
}
