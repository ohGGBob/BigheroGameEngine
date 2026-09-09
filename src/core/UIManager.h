#pragma once
#include <vector>
#include <string>
#include <cstddef>
#include "UIPanel.h"

namespace bighero {

// UIManager: top-level orchestrator for all UI panels / widgets. Owns a list
// of registered panels, tracks the focused/active one, and drives layout +
// z-order refresh. Pure stdlib, no rendering backend.
class UIManager {
public:
    UIManager() {}

    // Register a panel (returns its index id).
    std::size_t AddPanel(UIPanel* p) {
        panels_.push_back(p);
        return panels_.size() - 1;
    }
    bool RemovePanel(UIPanel* p) {
        for (auto it = panels_.begin(); it != panels_.end(); ++it) {
            if (*it == p) { panels_.erase(it); return true; }
        }
        return false;
    }
    std::size_t PanelCount() const { return panels_.size(); }
    UIPanel* Panel(std::size_t i) const {
        return i < panels_.size() ? panels_[i] : nullptr;
    }

    void SetFocused(UIPanel* p) { focused_ = p; }
    UIPanel* Focused() const { return focused_; }

    // Update visibility flags based on a focus pointer.
    void Update(bool mouseOverRoot = false) {
        rootHover_ = mouseOverRoot;
    }
    bool RootHover() const { return rootHover_; }

    void SortByZ() {
        // Simple insertion sort by z order (stable, small N typical).
        for (std::size_t i = 1; i < panels_.size(); ++i) {
            UIPanel* key = panels_[i];
            std::size_t j = i;
            while (j > 0 && ZOrderOf(panels_[j - 1]) > ZOrderOf(key)) {
                panels_[j] = panels_[j - 1];
                --j;
            }
            panels_[j] = key;
        }
    }

private:
    static int ZOrderOf(const UIPanel* p) { return p ? p->ZOrder() : 0; }

    std::vector<UIPanel*> panels_;
    UIPanel* focused_ = nullptr;
    bool rootHover_ = false;
};

} // namespace bighero
