#pragma once
#include <filesystem>
#include <string>
#include <fstream>
#include <format>
#include <boost/json.hpp>

inline auto readFile(const std::filesystem::path& path) -> std::string {

    std::ifstream file(path,std::ios::binary);
    if (!file)
        throw std::runtime_error(std::format("failed to open file at path: {}",path.string()));

    return std::string(std::istreambuf_iterator<char>(file), {});
}
inline auto readJson(const std::filesystem::path& path) -> boost::json::value {
    auto body = readFile(path);

    boost::system::error_code ec;
    auto v = boost::json::parse(body,ec);
    if (ec)
        throw std::runtime_error(std::format("invalid json in: {},{}",path.string(),ec.message()));
    if (!v.is_object())
        throw std::runtime_error(std::format("expected a json object in: {}",path.string()));

    return v;
}