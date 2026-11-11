#include "mustache.hpp"

#include <ranges>
#include <fstream>
#include <sstream>
#include <format>


namespace mustache {
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
    auto valueToString(const json::value &v) -> std::string {
        switch (v.kind()) {
            case json::kind::string:
                return std::string(v.get_string());
            case json::kind::bool_:
                return v.get_bool() ? "true" : "false";
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
        return {};
    }

    // decides whether a section should be shown
    auto isTruthy(const json::value *v) -> bool {
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
        return false;
    }

    // helper for findValue. recursion walk for a.b.c
    auto walk(const json::value *v, std::string_view sv) -> const json::value * {
        std::string_view rest{};
        if (const auto dot = sv.find('.'); dot != std::string_view::npos) {
            rest = sv.substr(dot + 1);
            sv = sv.substr(0, dot);
        }
        if (v->kind() == json::kind::object) {
            if (auto it = v->get_object().find(sv);
                it != v->get_object().cend()) {
                return rest.empty() ? &it->value() : walk(&it->value(), rest);
            }
        }
        return nullptr;
    }

    // finds the JSON val a tag like {{name}} or {{project.title}} refers to.
    auto findValue(const std::vector<const json::value *> &stack, std::string_view name) -> const json::value * {
        if (name == ".") return stack.back();
        if (const auto dot = name.find('.'); dot != std::string_view::npos) {
            const auto rest = name.substr(dot + 1);
            name = name.substr(0, dot);
            for (const auto s: stack | std::views::reverse) {
                if (s->kind() == json::kind::object) {
                    if (const auto it = s->get_object().find(name);
                        it != s->get_object().cend()) {
                        return walk(&it->value(), rest);
                    }
                }
            }
            return nullptr;
        }
        for (const auto s: stack | std::views::reverse) {
            if (s->kind() == json::kind::object) {
                if (const auto it = s->get_object().find(name); it != s->get_object().cend()) return &it->value();
            }
        }
        return nullptr;
    }

    auto trim(std::string_view s) -> std::string_view {
        auto start = s.find_first_not_of(" \t");
        if (start == std::string_view::npos) return "";
        auto end = s.find_last_not_of(" \t");
        return s.substr(start, end - start + 1);
    }

    auto classify(std::string_view s) -> Node {
        auto sv = trim(s);
        if (sv.empty()) throw ParseError("empty tag");

        Node::Kind kind{};
        switch (sv[0]) {
            case '&':
                kind = Node::Kind::Raw;
                break;
            case '>':
                kind = Node::Kind::Partial;
                break;
            case '#':
                kind = Node::Kind::Section;
                break;
            case '^':
                kind = Node::Kind::Inverted;
                break;
            default:
                kind = Node::Kind::Var;
                break;
        }
        if (kind != Node::Kind::Var) sv = trim(sv.substr(1));
        if (sv.empty()) throw ParseError("empty name");
        return Node{.kind = kind, .value = std::string(sv)};
    }

    auto lineOf(std::string_view src, size_t pos) -> int {
        return static_cast<int>(std::count(src.begin(), src.begin() + pos, '\n') + 1);
    }
}


// turns templated text into a list(tree) of Nodes
auto parse(std::string_view src) -> Nodes {
    // stack of "where new nodes go": the list we append to + the section name that owns it
    struct Pair {
        Nodes *list;
        std::string name;
    };
    Nodes nodes{};
    // starts at the root list; each {{#x}} pushes, each {{/x}} pops
    std::vector open{Pair{&nodes, ""}};
    std::size_t pos{};
    constexpr auto npos = std::string_view::npos;

    while (true) {
        auto openCurly = src.find("{{", pos);
        // no more tags: the rest is plain text
        if (openCurly == npos) {
            if (const auto sv = src.substr(pos); !sv.empty())
                open.back().list->emplace_back(Node::Kind::Text, std::string(sv));
            break;
        }
        // text between the previous tag and this one
        if (auto sv = src.substr(pos, openCurly - pos); !sv.empty()) {
            open.back().list->emplace_back(Node::Kind::Text, std::string(sv));
        }

        // {{{x}}} = raw (unescaped) variable, closes with }}}
        if (src.substr(openCurly).starts_with("{{{")) {
            auto closeCurly = src.find("}}}", openCurly + 3);
            if (closeCurly == npos) // Did not find a matching closing }}}
                throw ParseError(std::format("Did not find matching {} at {}", "}}}",
                                             mustache::lineOf(src, openCurly)));

            auto sv = mustache::trim(src.substr(openCurly + 3, closeCurly - (openCurly + 3)));
            if (sv.empty()) throw ParseError(std::format("empty name at {}", mustache::lineOf(src, closeCurly)));
            open.back().list->emplace_back(Node::Kind::Raw, std::string(sv));
            pos = closeCurly + 3;
            continue;
        }
        auto closeCurly = src.find("}}", openCurly + 2);
        if (closeCurly == npos) // Did not find a matching closing }}
            throw ParseError(std::format("Did not find matching {} at {}", "}}", mustache::lineOf(src, openCurly)));

        auto sv = src.substr(openCurly + 2, closeCurly - (openCurly + 2));
        auto tag = mustache::trim(sv);
        // end of section: must match the innermost open section
        if (tag.starts_with('/')) {
            tag = mustache::trim(tag.substr(1));
            if (tag.empty()) throw ParseError(std::format("empty name at {}", mustache::lineOf(src, closeCurly)));

            if (open.size() <= 1 || open.back().name != tag)
                throw ParseError(std::format("Did not find matching closing section '/' at {}",
                                             mustache::lineOf(src, pos)));

            open.pop_back();
            pos = closeCurly + 2;
            continue;
        }
        // comment: produces no node
        if (tag.starts_with('!')) {
            pos = closeCurly + 2;
            continue;
        }
        auto node = mustache::classify(sv);
        open.back().list->emplace_back(node);
        // enter the section: following nodes go into its children
        if (node.kind == Node::Kind::Section || node.kind == Node::Kind::Inverted) {
            open.emplace_back(&open.back().list->back().children, node.value);
        }

        pos = closeCurly + 2;
    }
    // a section was opened but never closed
    if (open.size() > 1) throw ParseError(std::format("unclosed section '/' {}", mustache::lineOf(src, pos)));
    return nodes;
}

// loads and parses every .html template under dir (recursively) once, at startup
TemplateSet::TemplateSet(const std::filesystem::path &dir) {
    for (const auto &entry: std::filesystem::recursive_directory_iterator(dir)) {
        // only regular .html files are templates
        if (!entry.is_regular_file() || entry.path().extension() != ".html") continue;
        // template name = path relative to dir, e.g. "partials/footer.html"
        auto key = std::filesystem::relative(entry.path(), dir).generic_string();

        // read the whole file into a string
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file) throw std::runtime_error("failed to open " + key);
        std::string body(std::istreambuf_iterator<char>(file), {});

        // parse once and cache the tree; prefix parse errors with the file name
        try {
            _templates[key] = parse(body);
        } catch (const ParseError &e) {
            throw ParseError(key + ": " + e.what());
        }
    }
}

// public entry: renders template `name` with `context` as the root scope
auto TemplateSet::render(const std::string &name, const json::value &context) const -> std::string {
    std::string out{};
    std::vector<const json::value *> stack{&context}; // scope stack, starts with the whole JSON
    renderNodes(_templates.at(name), stack, out, 0);

    return out;
}

// walks the tree and appends each node's output; recurses into section children
auto TemplateSet::renderNodes(const Nodes &nodes, std::vector<const json::value *> &stack, std::string &out,
                              int depth) const -> void {
    using namespace mustache;
    // guard against infinite recursion (e.g. a partial including itself)
    if (depth > 32) throw std::runtime_error("depth is over the limit");

    for (const auto &[kind, value, children]: nodes) {
        switch (kind) {
            // literal template text, copied as-is
            case Node::Kind::Text: {
                out += value;
                break;
            }
            // {{x}}: look up and append HTML-escaped; missing → nothing
            case Node::Kind::Var: {
                if (auto ptr = findValue(stack, value))
                    appendHtmlEscaped(out, valueToString(*ptr));
                break;
            }
            // {{{x}}} / {{&x}}: same as Var but without escaping
            case Node::Kind::Raw: {
                if (auto ptr = findValue(stack, value))
                    out += valueToString(*ptr);
                break;
            }
            // {{#x}}: skip if falsy; loop arrays, enter objects, else render once
            case Node::Kind::Section: {
                if (auto val = findValue(stack, value); isTruthy(val)) {
                    // array: render children once per item, item on top of the stack
                    if (val->is_array()) {
                        for (const auto &v: val->get_array()) {
                            stack.emplace_back(&v);
                            renderNodes(children, stack, out, depth + 1);
                            stack.pop_back();
                        }
                        // object: enter its scope once
                    } else if (val->is_object()) {
                        stack.emplace_back(val);
                        renderNodes(children, stack, out, depth + 1);
                        stack.pop_back();
                        // true / number / string: render once, no new scope
                    } else renderNodes(children, stack, out, depth + 1);
                }
                break;
            }
            // {{^x}}: render once only if x is falsy/missing
            case Node::Kind::Inverted: {
                if (!isTruthy(findValue(stack, value)))
                    renderNodes(children, stack, out, depth + 1);
                break;
            }
            // {{>name}}: render another template with the current scope stack
            case Node::Kind::Partial: {
                auto it = _templates.find(value);
                if (it == _templates.cend())
                    throw std::runtime_error(std::format("unknown partial {}", value));
                renderNodes(it->second, stack, out, depth + 1);
                break;
            }
        }
    }
}
