#pragma once

#include <stdexcept>
#include <string>
#include <vector>
#include <filesystem>

#include "http/common.hpp"

struct Node {
    enum class Kind {Text, Var, Raw, Section, Inverted, Partial};
    Kind kind{};
    std::string value{}; // Text: the text itself. every other kind the tag's name
    std::vector<Node> children{}; // only used by Section and Inverted.
};
using Nodes = std::vector<Node>;

// construct a new exception type in which behaves exactly like a runtime_error
struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

Nodes parse(std::string_view source);

class TemplateSet {
public:
    explicit TemplateSet(const std::filesystem::path& dir);
    auto redner(const std::string& name, const json::value& context) const -> std::string;

private:
    auto renderNodes(const Nodes& nodes, std::vector<const json::value*>& stack,
        std::string& out, int depth) const -> void;

    std::unordered_map<std::string,Nodes> _templates; // "index.html" -> its tree
};