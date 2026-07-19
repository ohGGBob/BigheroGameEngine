#pragma once
#include "ubo_buffer.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <stdexcept>
#include <utility>

namespace BigHero::Render
{
    /// 管理描述符集布局、池、集合，适配当前两套UBO资源
    class DescriptorManager
    {
    public:
        VkDevice device = VK_NULL_HANDLE;
        // 两套布局：set0 相机UBO，set1 光照UBO
        VkDescriptorSetLayout layoutCamera = VK_NULL_HANDLE;
        VkDescriptorSetLayout layoutLight = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorSet> descriptorSets;

        // 无参默认构造，延迟初始化
        DescriptorManager() = default;

        // 延迟初始化，必须传入合法VkDevice后再创建资源
        void Init(VkDevice dev)
        {
            device = dev;
            if (device == VK_NULL_HANDLE)
                throw std::runtime_error("DescriptorManager::Init: VkDevice is NULL! Initialize device first.");
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
            std::vector<VkDescriptorSetLayout> layouts = { layoutCamera, layoutLight };
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
            allocInfo.pSetLayouts = layouts.data();

            VkDescriptorSet sets[2];
            const VkResult res = vkAllocateDescriptorSets(device, &allocInfo, sets);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: Allocate descriptor set failed");

            descriptorSets.push_back(sets[0]);
            descriptorSets.push_back(sets[1]);
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
            device = VK_NULL_HANDLE;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return device != VK_NULL_HANDLE && layoutCamera != VK_NULL_HANDLE && layoutLight != VK_NULL_HANDLE && descriptorPool != VK_NULL_HANDLE;
        }

        [[nodiscard]] const std::vector<VkDescriptorSet>& GetSets() const noexcept
        {
            return descriptorSets;
        }

    private:
        void Swap(DescriptorManager& other) noexcept
        {
            std::swap(device, other.device);
            std::swap(layoutCamera, other.layoutCamera);
            std::swap(layoutLight, other.layoutLight);
            std::swap(descriptorPool, other.descriptorPool);
            std::swap(descriptorSets, other.descriptorSets);
        }

        /// 创建两套描述符布局，匹配着色器set/binding
        void CreateLayouts()
        {
            if (device == VK_NULL_HANDLE)
            {
                throw std::runtime_error("DescriptorManager::CreateLayouts: VkDevice is NULL! Initialize device first.");
            }
            // set=0 binding=0 : CameraUBO UniformBuffer
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
                throw std::runtime_error("DescriptorManager: Create camera set layout failed");

            // set=1 binding=0 : LightUBO UniformBuffer
            VkDescriptorSetLayoutBinding lightBinding{};
            lightBinding.binding = 0;
            lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            lightBinding.descriptorCount = 1;
            lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            lightBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo lightLayoutInfo{};
            lightLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lightLayoutInfo.bindingCount = 1;
            lightLayoutInfo.pBindings = &lightBinding;
            res = vkCreateDescriptorSetLayout(device, &lightLayoutInfo, nullptr, &layoutLight);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: Create light set layout failed");
        }

        /// 创建描述符池，预留足够数量的UBO描述符
        void CreateDescriptorPool()
        {
            std::vector<VkDescriptorPoolSize> poolSizes = {
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100} // 最多支持100组UBO，满足多物体场景
            };

            VkDescriptorPoolCreateInfo poolInfo{};
            poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
            poolInfo.pPoolSizes = poolSizes.data();
            poolInfo.maxSets = 200; // 最大描述符集数量

            const VkResult res = vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool);
            if (res != VK_SUCCESS)
                throw std::runtime_error("DescriptorManager: Create descriptor pool failed");
        }
    };
}