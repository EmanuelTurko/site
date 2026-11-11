#include "mustache.hpp"

#include <ranges>


namespace {
    // appends s to the end of out. replaces special characters that mean something in HTML - prevents XSS
    auto appendHtmlEscaped(std::string &out, std::string_view s) -> void {
        for (const auto c: s) {
            switch (c) {
                case '&': out += "&amp;";
                    break;
                case '<': out += "&lt;";
                    break;
                case '>': out += "&gt;";
                    break;
                case '"': out += "&quot;";
                    break;
                case '\'': out += "&#39;";
                    break;
                default: out += c;
                    break;
            }
        }
    }
    // turns any JSON val into the text that should appear
    auto valueToString(const json::value& v) -> std::string {
        switch (v.kind()) {
            case json::kind::string:
                return std::string(v.get_string());
            case json::kind::bool_:
                return v.get_bool()? "true" : "false";
            case json::kind::int64:
               return std::to_string(v.get_int64());
            case json::kind::uint64:
                return std::to_string(v.get_uint64());
            case json::kind::null:
            case json::kind::array:
            case json::kind::object:
                return "";
            case json::kind::double_: {
                std::ostringstream oss;
                oss << v.get_double();
                return oss.str();
            }
        }
    }

    // decides whether a section should be shown
    auto isTruthy(const json::value* v) -> bool {
        if (!v) return false;

        switch (v->kind()) {
            case json::kind::null:
            return false;
            case json::kind::bool_:
                return v->get_bool();
            case json::kind::array:
                return !v->get_array().empty();
            case json::kind::string:
                return !v->get_string().empty();
            case json::kind::int64:
            case json::kind::uint64:
            case json::kind::double_:
            case json::kind::object:
                return true;
        }
    }
    // helper for findValue. recursion walk for a.b.c
    auto walk(const json::value* v,std::string_view sv) -> const json::value* {
        std::string_view rest{};
        if (const auto dot = sv.find('.'); dot != std::string_view::npos) {
            rest = sv.substr(dot +1);
            sv = sv.substr(0,dot);
        }
        if (v->kind() == json::kind::object) {
            if (auto it = v->get_object().find(sv);
                it != v->get_object().cend()) {
                return rest.empty()? &it->value() : walk(&it->value(),rest);
                }
        }
        return nullptr;
    }

    // finds the JSON val a tag like {{name}} or {{project.title}} refers to.
    auto findValue(const std::vector<const json::value*>& stack,std::string_view name) -> const json::value* {

        if (name == ".") return stack.back();
        if (const auto dot = name.find('.'); dot != std::string_view::npos) {
            const auto rest = name.substr(dot +1);
            name = name.substr(0,dot);
            for (const auto s : stack | std::views::reverse) {
                if (s->kind() == json::kind::object) {
                    if (const auto it = s->get_object().find(name);
                        it != s->get_object().cend()) {
                        return walk(&it->value(),rest);
                    }
                }
            }
            return nullptr;
        }
        for (const auto s : stack | std::views::reverse) {
            if (s->kind() == json::kind::object) {
                if (const auto it = s->get_object().find(name); it!= s->get_object().cend()) return &it->value();
            }
        }
        return nullptr;
    }
}
auto trim(std::string_view s) -> std::string_view {
    auto start = s.find_first_not_of(" \t");
    if (start == std::string_view::npos) return "";
    auto end = s.find_last_not_of(" \t");
    return s.substr(start,end - start +1);

}
auto classify(std::string_view s) -> Node {
    const auto sv = trim(s);
    if (sv.empty()) throw ParseError("empty tag");

    switch (sv[0]) {
        case '&':
            return Node{
                .kind = Node::Kind::Raw,
                .value = std::string(trim(sv.substr(1)))};
        case '>':
            return Node{
                .kind = Node::Kind::Partial,
                .value = std::string(trim(sv.substr(1)))};
        case '#':
            return Node{
                .kind = Node::Kind::Section,
                .value = std::string(trim(sv.substr(1)))};
        case '^':
            return Node{
                .kind = Node::Kind::Inverted,
                .value = std::string(trim(sv.substr(1)))};
        default:
            return Node{
                .kind = Node::Kind::Var,
                .value = std::string(sv)};
    }
}

// turns templated text into a list(tree) of Nodes
auto parse(std::string_view src) -> Nodes {
    struct Pair {
        Nodes *list;
        std::string name;
    };
    Nodes nodes{};
    std::vector open{Pair{&nodes, ""}};
    std::size_t pos{};
    constexpr auto npos = std::string_view::npos;


    while (true) {
        auto openCurly = src.find("{{", pos);
        if (openCurly == npos) {
            if (const auto sv = src.substr(pos); !sv.empty())
                open.back().list->emplace_back(Node::Kind::Text, std::string(sv));
            break;
        }
        if (auto sv = src.substr(pos, openCurly - pos); !sv.empty()) {
            open.back().list->emplace_back(Node::Kind::Text, std::string(sv));
        }

        if (src.substr(openCurly).starts_with("{{{")) {
            auto closeCurly = src.find("}}}", openCurly + 3);
            if (closeCurly == npos) // Did not find a matching closing }}}
                throw ParseError("Did not find matching }}}"); // TODO add line num

            auto sv = trim(src.substr(openCurly + 3, closeCurly - (openCurly + 3)));
            open.back().list->emplace_back(Node::Kind::Raw, std::string(sv));
            pos = closeCurly + 3;
            continue;
        }
        auto closeCurly = src.find("}}", openCurly + 2);
        if (closeCurly == npos) // Did not find a matching closing }}
            throw ParseError("Did not find matching }}"); // TODO add line num

        auto sv = src.substr(openCurly + 2, closeCurly - (openCurly + 2));
        auto tag = trim(sv);
        if (tag.starts_with('/')) {
            tag = trim(tag.substr(1));

            if (open.size() <= 1 || open.back().name != tag)
                throw ParseError("Did not find matching closing section '/'");

            open.pop_back();
            pos = closeCurly + 2;
            continue;
        }
        if (tag.starts_with('!')) {
            pos = closeCurly + 2;
            continue;
        }
        auto node = classify(sv);
        open.back().list->emplace_back(node);
        if (node.kind == Node::Kind::Section || node.kind == Node::Kind::Inverted) {
            open.emplace_back(&open.back().list->back().children, node.value);
        }

        pos = closeCurly + 2;
    }
    if (open.size() > 1) throw ParseError("unclosed section");
    return nodes;
}
