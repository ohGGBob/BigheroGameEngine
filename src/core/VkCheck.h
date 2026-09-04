#pragma once
#include "core/Log.h"
#include "core/VkUtils.h"
#include <stdexcept>
#include <string>
#include <vulkan/vulkan.h>

// 检查VkResult：失败时记录日志并抛出异常，由调用栈上层的RAII保证已创建资源被回收
#define VK_CHECK(result, msg)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        const VkResult _vkCheckRes_ = (result);                                                                        \
        if (_vkCheckRes_ != VK_SUCCESS)                                                                                \
        {                                                                                                              \
            LOG_ERROR(msg << " | VkResult: " << ::BigHero::VkResultToString(_vkCheckRes_));                            \
            throw std::runtime_error(std::string("[VK_CHECK] ") + std::string(msg));                                   \
        }                                                                                                              \
    } while (0)

