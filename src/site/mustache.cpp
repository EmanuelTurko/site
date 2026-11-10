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
