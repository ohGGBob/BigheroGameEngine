#pragma once
#include <string>
#include <vector>

namespace bighero {

// UIText: a screen-space text label with layout, alignment, color and
// wrapping metadata. Pure data container; glyph rasterization is external.
class UIText {
public:
    UIText() {}
    explicit UIText(const std::string& content) : content_(content) {}

    void SetContent(const std::string& c) { content_ = c; }
    const std::string& Content() const { return content_; }
    void Append(const std::string& c) { content_ += c; }

    void SetRect(float x, float y, float w, float h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void Rect(float& x, float& y, float& w, float& h) const { x = x_; y = y_; w = w_; h = h_; }

    void SetFontSize(float s) { fontSize_ = s < 0 ? 0 : s; }
    float FontSize() const { return fontSize_; }

    void SetColor(float r, float g, float b, float a = 1.0f) {
        r_=r; g_=g; b_=b; a_=a;
    }
    void Color(float& r, float& g, float& b, float& a) const { r=r_; g=g_; b=b_; a=a_; }

    void SetBold(bool b) { bold_ = b; }
    bool Bold() const { return bold_; }
    void SetAlign(int align) { align_ = align; } // 0=left 1=center 2=right
    int Align() const { return align_; }
    void SetWrap(bool w) { wrap_ = w; }
    bool Wrap() const { return wrap_; }
    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const { return visible_; }

    // Approximate wrapped-line count given a character-per-line estimate.
    int EstimatedLineCount(int charsPerLine) const {
        if (charsPerLine <= 0) return 1;
        int lines = 0;
        std::vector<std::string> paras;
        std::string cur;
        for (char c : content_) {
            if (c == '\n') { paras.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        paras.push_back(cur);
        for (auto& p : paras) {
            if (!wrap_) { ++lines; continue; }
            int n = (int)p.size();
            lines += n / charsPerLine + (n % charsPerLine ? 1 : 0);
            if (lines == 0) lines = 1;
        }
        return lines;
    }

private:
    std::string content_;
    float x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    float fontSize_ = 14.0f;
    float r_ = 1, g_ = 1, b_ = 1, a_ = 1;
    bool bold_ = false;
    int align_ = 0;
    bool wrap_ = false;
    bool visible_ = true;
};

} // namespace bighero
