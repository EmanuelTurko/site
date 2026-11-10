#pragma once

#include "common.hpp"
#include <string>
#include <filesystem>

auto staticFiles(std::filesystem::path dir, std::string urlPrefix) -> Handler;