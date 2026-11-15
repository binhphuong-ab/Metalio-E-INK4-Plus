#include "html_content.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace reader {
namespace {

std::string ToLower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool StartsWithIgnoreCase(const char* p, const char* lit) {
    while (*lit) {
        if (std::tolower(static_cast<unsigned char>(*p)) !=
            std::tolower(static_cast<unsigned char>(*lit))) {
            return false;
        }
        ++p;
        ++lit;
    }
    return true;
}

std::string ReadAttrValue(const char* tag_start, const char* tag_end, const char* attr) {
    const size_t alen = std::strlen(attr);
    for (const char* q = tag_start; q + alen + 2 < tag_end; ++q) {
        if (!StartsWithIgnoreCase(q, attr)) {
            continue;
        }
        if (q > tag_start && (std::isalnum(static_cast<unsigned char>(q[-1])) || q[-1] == '_' ||
                              q[-1] == '-' || q[-1] == ':')) {
            continue;
        }
        q += alen;
        while (q < tag_end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
            ++q;
        }
        if (q >= tag_end || *q != '=') {
            continue;
        }
        ++q;
        while (q < tag_end && (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
            ++q;
        }
        if (q >= tag_end) {
            return {};
        }
        char quote = 0;
        if (*q == '"' || *q == '\'') {
            quote = *q++;
        }
        const char* p = q;
        std::string val;
        while (p < tag_end) {
            if (quote) {
                if (*p == quote) {
                    break;
                }
            } else if (*p == ' ' || *p == '\t' || *p == '>' || *p == '/') {
                break;
            }
            val.push_back(*p++);
        }
        return val;
    }
    return {};
}

bool IsBlockEndTag(const std::string& name) {
    return name == "p" || name == "div" || name == "br" || name == "li" || name == "tr" ||
           name == "h1" || name == "h2" || name == "h3" || name == "h4" || name == "h5" ||
           name == "h6" || name == "section" || name == "article" || name == "blockquote";
}

}  // namespace

std::string DecodeHtmlEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') {
            out.push_back(in[i]);
            continue;
        }
        const size_t semi = in.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 16) {
            out.push_back(in[i]);
            continue;
        }
        const std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp") {
            out.push_back('&');
        } else if (ent == "lt") {
            out.push_back('<');
        } else if (ent == "gt") {
            out.push_back('>');
        } else if (ent == "quot") {
            out.push_back('"');
        } else if (ent == "apos") {
            out.push_back('\'');
        } else if (ent == "nbsp") {
            out.push_back(' ');
        } else if (!ent.empty() && ent[0] == '#') {
            const bool hexadecimal = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
            const char* number = ent.c_str() + (hexadecimal ? 2 : 1);
            char* end = nullptr;
            const unsigned long code = std::strtoul(number, &end, hexadecimal ? 16 : 10);
            if (end == number || *end != '\0' || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
                out.push_back('?'); i = semi; continue;
            }
            if (code > 0 && code < 128) {
                out.push_back(static_cast<char>(code));
            } else if (code >= 128 && code <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code > 0x7FF && code <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else if (code > 0xFFFF) {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.push_back('?');
            }
        } else {
            out.push_back('&');
            out.append(ent);
            out.push_back(';');
            i = semi;
            continue;
        }
        i = semi;
    }
    return out;
}

std::string DecodeUrlComponent(const std::string& in) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string out;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '%') { if (!in[i]) return {}; out.push_back(in[i]); continue; }
        if (i + 2 >= in.size()) return {};
        const int hi = hex(in[i + 1]), lo = hex(in[i + 2]);
        if (hi < 0 || lo < 0 || hi * 16 + lo == 0) return {};
        out.push_back(static_cast<char>(hi * 16 + lo)); i += 2;
    }
    return out;
}

std::string NormalizeZipPath(const std::string& base_dir, const std::string& href) {
    const size_t end = href.find_first_of("#?");
    std::string raw = DecodeUrlComponent(href.substr(0, end));
    if (raw.empty() || raw.find(':') != std::string::npos || raw.compare(0, 2, "//") == 0) return {};
    const std::string path = raw.front() == '/' ? raw : base_dir + (base_dir.empty() || base_dir.back() == '/' ? "" : "/") + raw;
    std::vector<std::string> parts;
    for (size_t i = 0; i < path.size();) {
        size_t j = path.find('/', i); if (j == std::string::npos) j = path.size();
        const std::string part = path.substr(i, j - i); i = j + 1;
        if (part.empty() || part == ".") continue;
        if (part == "..") { if (parts.empty()) return {}; parts.pop_back(); }
        else parts.push_back(part);
    }
    std::string out;
    for (const auto& part : parts) { if (!out.empty()) out += '/'; out += part; }
    return out;
}

bool HtmlBlockParser::Emit(bool more) {
    if (!more) while (!text_.empty() && (text_.back() == ' ' || text_.back() == '\n')) text_.pop_back();
    if (text_.empty() && !continuation_) return true;
    ContentBlock block;
    block.chapter_title = in_heading_;
    block.boxed = boxed_nest_ > 0;
    block.text.swap(text_); block.continuation = continuation_; block.continues = more;
    const uint32_t length = block.text.size();
    if (!sink_(std::move(block))) return false;
    if (more) source_offset_ += length;
    else { ++source_block_; source_offset_ = 0; }
    continuation_ = more;
    if (!more) last_space_ = true;
    return true;
}

bool HtmlBlockParser::Append(char c) {
    // UTF-8 最多跨分片边界补齐三个字节，异常续字节不能无限扩展缓冲。
    if (text_.size() >= 8195 && (static_cast<uint8_t>(c) & 0xC0) == 0x80) return false;
    if (text_.size() >= 8192 && (static_cast<uint8_t>(c) & 0xC0) != 0x80 && !Emit(true)) return false;
    text_.push_back(c);
    last_space_ = c == ' ' || c == '\n';
    return true;
}

bool HtmlBlockParser::Text(char c) {
    if (c == '\r') return true;
    if (c == '\n' || c == '\t') c = ' ';
    if (c == ' ' && last_space_) return true;
    return Append(c);
}

void HtmlBlockParser::Anchor(const char* begin, const char* end) {
    if (!anchors_) return;
    std::string id = ReadAttrValue(begin, end, "id");
    if (id.empty()) id = ReadAttrValue(begin, end, "xml:id");
    if (id.empty()) id = ReadAttrValue(begin, end, "name");
    if (!id.empty()) anchors_(DecodeHtmlEntities(id), source_block_, source_offset_ + text_.size());
}

// True when the attribute is present, with or without a value (e.g. bare `hidden`).
// Quoted attribute values are skipped so text like title="hidden" does not match.
static bool HasAttr(const char* p, const char* end, const char* attr) {
    const size_t n = std::strlen(attr);
    char quote = 0;
    for (; p < end; ++p) {
        if (quote) { if (*p == quote) quote = 0; continue; }
        if (*p == '"' || *p == '\'') { quote = *p; continue; }
        if (!std::isspace(static_cast<unsigned char>(*p))) continue;
        const char* a = p + 1;
        if (static_cast<size_t>(end - a) < n || strncasecmp(a, attr, n) != 0) continue;
        const char after = a + n < end ? a[n] : '\0';
        if (after == '\0' || after == '=' || after == '/' || std::isspace(static_cast<unsigned char>(after))) return true;
    }
    return false;
}

// True when the class attribute holds `token` as a whole whitespace-separated word.
static bool HasClassToken(const char* begin, const char* end, const char* token) {
    const std::string cls = ToLower(ReadAttrValue(begin, end, "class"));
    const size_t n = std::strlen(token);
    for (size_t i = cls.find(token); i != std::string::npos; i = cls.find(token, i + 1)) {
        const bool left = i == 0 || std::isspace(static_cast<unsigned char>(cls[i - 1]));
        const bool right = i + n == cls.size() || std::isspace(static_cast<unsigned char>(cls[i + n]));
        if (left && right) return true;
    }
    return false;
}

// Elements that EPUB/HTML say must not be rendered. EPUB 3 nav documents in the
// spine typically carry a hidden page-list (hundreds of print page numbers) and
// landmarks; rendering them produced dozens of pages of bare numbers.
static bool IsHiddenElement(const std::string& name, const char* begin, const char* end) {
    if (HasAttr(begin, end, "hidden")) return true;
    std::string style = ToLower(ReadAttrValue(begin, end, "style"));
    style.erase(std::remove_if(style.begin(), style.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }), style.end());
    if (style.find("display:none") != std::string::npos) return true;
    if (name == "nav") {
        const std::string type = ToLower(ReadAttrValue(begin, end, "epub:type"));
        if (type.find("page-list") != std::string::npos || type.find("landmarks") != std::string::npos) return true;
    }
    return false;
}

bool HtmlBlockParser::Tag() {
    const char* begin = tag_.data();
    const char* end = begin + tag_.size();
    bool closing = begin < end && *begin == '/';
    if (closing) ++begin;
    while (begin < end && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
    const char* p = begin;
    while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == ':' || *p == '-' || *p == '_')) ++p;
    std::string name = ToLower(std::string(begin, p));
    const size_t colon = name.find(':');
    if (colon != std::string::npos) name.erase(0, colon + 1);
    const bool self_closing = end > begin && *(end - 1) == '/';

    if (hidden_nest_ > 0) {
        // Inside a hidden subtree: only track nesting of its root tag name.
        if (name == hidden_tag_) {
            if (closing) { if (--hidden_nest_ == 0) hidden_tag_.clear(); }
            else if (!self_closing) ++hidden_nest_;
        }
        return true;
    }
    if (!closing && !in_head_ && !in_script_ && !in_style_ && !name.empty() &&
        IsHiddenElement(name, p, end)) {
        if (self_closing || name == "img" || name == "image" || name == "br") return true;
        if (!Emit(false)) return false;
        hidden_tag_ = name;
        hidden_nest_ = 1;
        return true;
    }

    if (name == "head") in_head_ = !closing;
    else if (name == "script") in_script_ = !closing;
    else if (name == "style") in_style_ = !closing;
    else if (!in_head_ && !in_script_ && !in_style_) {
        if (!closing && !self_closing && !name.empty()) {
            if (boxed_nest_ > 0) {
                if (name == boxed_tag_) ++boxed_nest_;
            } else if (HasClassToken(p, end, "ap-note")) {
                // The box starts a fresh block so preceding text stays outside the frame.
                if (!Emit(false)) return false;
                boxed_tag_ = name;
                boxed_nest_ = 1;
            }
        }
        const bool heading = name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6';
        if (heading && !closing) in_heading_ = text_.empty() && !continuation_;
        if (!closing && (name == "img" || name == "image")) {
            if (!Emit(false)) return false;
            Anchor(begin, end);
            std::string src = ReadAttrValue(begin, end, "src");
            if (src.empty()) src = ReadAttrValue(begin, end, "xlink:href");
            if (src.empty()) src = ReadAttrValue(begin, end, "href");
            ContentBlock image; image.kind = ContentKind::kImage;
            image.image_href = NormalizeZipPath(base_, DecodeHtmlEntities(src));
            if (!image.image_href.empty()) {
                if (!sink_(std::move(image))) return false;
                ++source_block_; source_offset_ = 0;
            }
        } else if (name == "br") {
            if (!closing) Anchor(begin, end);
            if (!Append('\n')) return false;
        } else if (closing && IsBlockEndTag(name)) {
            if (!Emit(false)) return false;
            if (heading) in_heading_ = false;
        } else if (!closing && IsBlockEndTag(name) && name != "br" && !text_.empty()) {
            if (!Append('\n')) return false;
            Anchor(begin, end);
        } else if (!closing) Anchor(begin, end);
        if (closing && boxed_nest_ > 0 && name == boxed_tag_) {
            // Flush boxed text (inline box roots too) before leaving the box.
            if (boxed_nest_ == 1 && !Emit(false)) return false;
            if (--boxed_nest_ == 0) boxed_tag_.clear();
        }
    }
    return true;
}

bool HtmlBlockParser::Feed(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        const char c = static_cast<char>(data[i]);
        if (in_tag_) {
            if (comment_) {
                tag_.push_back(c);
                if (tag_.size() > 3) tag_.erase(0, tag_.size() - 3);
                if (tag_ == "-->") { comment_ = in_tag_ = false; tag_.clear(); }
                continue;
            }
            if (c == '>' && !quote_) {
                if (!Tag()) return false;
                tag_.clear(); in_tag_ = false;
            } else {
                if (tag_.size() >= 8192) return false;
                tag_.push_back(c);
                if (tag_ == "!--") { comment_ = true; tag_.clear(); }
                else if (quote_ && c == quote_) quote_ = 0;
                else if (!quote_ && (c == '\'' || c == '"')) quote_ = c;
            }
            continue;
        }
        if (!entity_.empty()) {
            if (c == ';') {
                entity_.push_back(c);
                for (char out : DecodeHtmlEntities(entity_)) if (!Text(out)) return false;
                entity_.clear(); continue;
            }
            if (entity_.size() < 16 && c != '<' && c != '&' && !std::isspace(static_cast<unsigned char>(c))) {
                entity_.push_back(c); continue;
            }
            for (char out : entity_) if (!Text(out)) return false;
            entity_.clear();
        }
        if (c == '<') { in_tag_ = true; continue; }
        if (in_head_ || in_script_ || in_style_ || hidden_nest_ > 0) continue;
        if (c == '&') entity_ = "&";
        else if (!Text(c)) return false;
    }
    return true;
}

bool HtmlBlockParser::Finish() {
    if (in_tag_) return false;
    for (char c : entity_) if (!Text(c)) return false;
    entity_.clear();
    return Emit(false);
}

} // namespace reader
