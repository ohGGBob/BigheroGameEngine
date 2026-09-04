#pragma once
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
namespace detail
{
// 4字节对齐SPIR-V存储容器，符合Vulkan规范
struct SpirvBuffer
{
    std::vector<std::uint32_t> data;

    [[nodiscard]] const std::vector<std::uint32_t>& GetData() const noexcept { return data; }

    [[nodiscard]] size_t GetByteSize() const noexcept { return data.size() * sizeof(std::uint32_t); }

    static SpirvBuffer LoadFromFile(const std::string& filePath)
    {
        std::ifstream file(filePath, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("Shader IO Error: Cannot open file -> " + filePath);
        }

        const std::streamsize rawByteCount = file.tellg();
        if (rawByteCount <= 0)
        {
            throw std::runtime_error("Shader IO Error: Empty shader file -> " + filePath);
        }
        file.seekg(0);

        // 向上对齐至4字节
        constexpr size_t alignStep = 4;
        const size_t uint32Count = (static_cast<size_t>(rawByteCount) + alignStep - 1) / alignStep;
        SpirvBuffer buffer{};
        buffer.data.resize(uint32Count, 0);

        // 读取二进制字节
        file.read(reinterpret_cast<char*>(buffer.data.data()), rawByteCount);
        if (!file.good())
        {
            throw std::runtime_error("Shader IO Error: Failed to read file -> " + filePath);
        }

        return buffer;
    }
};
} // namespace detail

/// 加载SPIR-V着色器二进制文件，自动4字节对齐
[[nodiscard]] inline detail::SpirvBuffer ReadShaderFile(const std::string& filePath)
{
    return detail::SpirvBuffer::LoadFromFile(filePath);
}

/// 创建Vulkan着色器模块
/// @param device 逻辑设备句柄
/// @param spirvBuffer 对齐合法SPIR-V字节码容器
/// @return VkShaderModule 原始句柄，必须手动 vkDestroyShaderModule 释放
[[nodiscard]] inline VkShaderModule CreateShaderModule(VkDevice device, const detail::SpirvBuffer& spirvBuffer)
{
    const auto& code = spirvBuffer.GetData();
    if (code.empty())
    {
        throw std::runtime_error("Shader Create Error: SPIR-V binary data is empty");
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirvBuffer.GetByteSize();
    createInfo.pCode = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Shader Create Error: vkCreateShaderModule failed");
    }
    return shaderModule;
}

/// RAII自动管理VkShaderModule，离开作用域自动销毁，杜绝泄漏
struct ShaderModuleHandle
{
    VkDevice device = VK_NULL_HANDLE;
    VkShaderModule module = VK_NULL_HANDLE;

    ShaderModuleHandle() = default;

    ShaderModuleHandle(VkDevice dev, const detail::SpirvBuffer& spirvData) : device(dev)
    {
        module = CreateShaderModule(dev, spirvData);
    }

    // 禁止拷贝
    ShaderModuleHandle(const ShaderModuleHandle&) = delete;
    ShaderModuleHandle& operator=(const ShaderModuleHandle&) = delete;

    // 移动构造
    ShaderModuleHandle(ShaderModuleHandle&& other) noexcept
    {
        device = other.device;
        module = other.module;
        other.device = VK_NULL_HANDLE;
        other.module = VK_NULL_HANDLE;
    }

    // 移动赋值
    ShaderModuleHandle& operator=(ShaderModuleHandle&& other) noexcept
    {
        if (this == &other)
            return *this;

        Release();
        device = other.device;
        module = other.module;
        other.device = VK_NULL_HANDLE;
        other.module = VK_NULL_HANDLE;
        return *this;
    }

    ~ShaderModuleHandle() { Release(); }

    /// 手动释放着色器模块
    void Release() noexcept
    {
        if (device != VK_NULL_HANDLE && module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(device, module, nullptr);
            module = VK_NULL_HANDLE;
        }
    }

    /// 隐式转换为原生VkShaderModule，直接传入管线创建结构
    operator VkShaderModule() const noexcept { return module; }

    /// 判断当前句柄是否有效
    [[nodiscard]] bool IsValid() const noexcept { return module != VK_NULL_HANDLE; }
};
} // namespace BigHero::Render
