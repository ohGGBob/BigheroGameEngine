#pragma once
// 队列族选型：从物理设备队列族属性中挑选"专用传输族"，为把瞬态上传从图形队列卸载到独立
// DMA 队列做准备。纯逻辑（不触碰 GPU），可离线单测。
//
// 背景（深化2 · RenderGraph 跨队列实际接线）：
//   B2(282cd24) 已让 RenderGraph 自动生成 image 的 queue family ownership transfer barrier。
//   本模块负责"选哪个族做 transfer 队列"这一纯决策，与运行时枚举解耦，便于单测。
//
// 选型优先级：
//   1) TRANSFER_BIT 且不含 GRAPHICS_BIT 且不含 COMPUTE_BIT —— 纯传输 DMA 队列（最佳）；
//   2) TRANSFER_BIT 且不含 GRAPHICS_BIT（可同时 compute 的独立传输族，次优）。
// 命中族下标与 graphicsFamily 不同即"专用传输族"；否则返回 UINT32_MAX（无独立传输能力，
// 回退图形队列，行为与单队列设备一致）。

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace BigHero::Render
{
[[nodiscard]] uint32_t SelectDedicatedTransferFamily(const std::vector<VkQueueFamilyProperties>& queues,
                                                     uint32_t graphicsFamily) noexcept;
} // namespace BigHero::Render
