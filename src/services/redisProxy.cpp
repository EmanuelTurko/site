#include "redisProxy.hpp"

namespace {
    struct CommandSpec {
        std::size_t argc;
        bool hasKey;
    };
    const std::unordered_map<std::string,CommandSpec> kCommands = {
        {"get", {2,true}},
        {"del", {2,true}},
        {"pttl", {2,true}},
        {"set", {3,true}},
        {"pexpire", {3,true}},
        {"zrem", {3,true}},
        {"zscore", {3,true}},
        {"zadd", {4,true}},
        {"zquery", {6,true}},
        {"keys", {1,false}},
    };

    auto splitIntoArgs(std::string_view sv) -> std::vector<std::string> {
        std::vector<std::string> v{};
        constexpr auto npos = std::string_view::npos;
        while (true) {
            auto start = sv.find_first_not_of(" \t");
            if (start == npos) break;
            sv = sv.substr(start);

            if (sv[0] == '"') {
                auto end = sv.find('"',1);
                if (end == npos) throw std::runtime_error("unclosed quotes");

                v.emplace_back(sv.substr(1,end-1));
                sv = sv.substr(end+1);
                continue;
            }
            auto end = sv.find_first_of(" \t");
            v.emplace_back(sv.substr(0,end));

            if (end == npos) break;
            sv = sv.substr(end);
        }
        return v;
    }
    auto isValidSession(std::string_view sv) -> bool {
        if (sv.length() > 32 || sv.length() < 8) return false;
        auto it = sv.find_first_not_of("0123456789abcdef");
        return it == std::string_view::npos;
    }

    auto toHexString(std::string_view bytes) -> std::string {
        std::string out{};

        for (auto b : bytes) {
            if (!out.empty()) out+= ' ';
            out += std::format("{:02x}",static_cast<unsigned char>(b));
        }
        return out;
    }

    auto segmentsToJson(const std::vector<RedisProxy::Segment>& segments) -> json::array{
        json::array arr{};
        for (const auto&[label, bytes] : segments) {
            json::object obj{
                {"label", label},
                {"hex", toHexString(bytes)}
            };
            arr.emplace_back(std::move(obj));
        }
        return arr;
    }
}
