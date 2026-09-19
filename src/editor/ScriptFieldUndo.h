#pragma once
// 脚本字段编辑的可撤销命令（U1-S1d）。
//
// 与 SceneSnapshotCommand 的关系：SceneSnapshot 只覆盖场景包（SceneObject/自转角），
// C# 脚本字段值存放在托管实例（GCHandle 表），不进场景快照——故脚本字段手势走本命令：
//   - 提交：Application 的 HandlePropertyEditUndo 在手势结束边沿比对"Update 阶段逐帧
//     拉取的绘制前基线 vs 松手后现值"，有差异则定格 绑定身份 + before/after 值表 入栈；
//   - Do/Undo：经 CSharpHost::ApplyFieldValues 按（实体稳定序下标, 类型名）重定位当前
//     绑定后逐字段写回托管实例（Inspector 下行通道，写回当帧脚本即读到新值）。
// 热重载语义：重载 = 实例重建、字段回落默认值（与 Unity 域重载一致）；跨重载的撤销命令
// 按身份尽力重定位，字段表变化时越界部分跳过（ApplyFieldValues 防御）。

#include "game/CommandStack.h"
#include "script/CSharpHost.h"

#include <string>
#include <utility>
#include <vector>

namespace BigHero::Game
{
class ScriptFieldValuesCommand : public Command
{
  public:
    ScriptFieldValuesCommand(Script::CSharpHost* host, std::vector<Script::BindingId> bindings,
                             std::vector<Script::ScriptFieldTable> before,
                             std::vector<Script::ScriptFieldTable> after, const char* name)
        : host_(host), bindings_(std::move(bindings)), before_(std::move(before)), after_(std::move(after)),
          name_(name)
    {
    }

    void Do() override
    {
        if (host_ != nullptr)
            (void)host_->ApplyFieldValues(bindings_, after_);
    }

    void Undo() override
    {
        if (host_ != nullptr)
            (void)host_->ApplyFieldValues(bindings_, before_);
    }

    [[nodiscard]] const char* Name() const noexcept override { return name_; }

  private:
    Script::CSharpHost* host_;
    std::vector<Script::BindingId> bindings_;
    std::vector<Script::ScriptFieldTable> before_;
    std::vector<Script::ScriptFieldTable> after_;
    const char* name_;
};
} // namespace BigHero::Game
