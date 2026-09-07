#pragma once
// 属性包（PropertyBag）：以字符串键存储任意值的通用键值容器。
// 纯标准库、仅头文件。
//
// 商业化价值：配置系统、场景序列化、组件属性面板、跨系统传递参数；
// 用 Any 包装任意类型，按字符串键读写，比固定结构体更灵活。
//
// 提供：Set(key, value)、Has(key)、Get<T>(key)、GetOr<T>(key, default)、Erase(key)、Size、Clear。
// 底层用 unordered_map<string, Any>（依赖 Any.h）。

#include "Any.h"
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace BigHero::Core
{
class PropertyBag
{
  public:
    template<typename T> PropertyBag& Set(std::string_view key, T&& value)
    {
        props_[std::string(key)].Set(std::forward<T>(value));
        return *this;
    }

    bool Has(std::string_view key) const
    {
        return props_.find(std::string(key)) != props_.end();
    }

    // 读取；key 不存在或类型不匹配返回默认值。
    template<typename T> T GetOr(std::string_view key, const T& def) const
    {
        auto it = props_.find(std::string(key));
        if (it == props_.end())
            return def;
        T out;
        if (it->second.TryGet<T>(out))
            return out;
        return def;
    }

    // 读取；不存在或类型不匹配抛异常。
    template<typename T> const T& Get(std::string_view key) const
    {
        auto it = props_.find(std::string(key));
        if (it == props_.end())
            throw std::out_of_range("PropertyBag key not found");
        return it->second.Get<T>();
    }

    bool Erase(std::string_view key)
    {
        return props_.erase(std::string(key)) > 0;
    }

    [[nodiscard]] size_t Size() const { return props_.size(); }
    void Clear() { props_.clear(); }

    const std::unordered_map<std::string, Any>& All() const { return props_; }

  private:
    std::unordered_map<std::string, Any> props_;
};
} // namespace BigHero::Core
