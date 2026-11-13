#include "redisProxy.hpp"
#include <cstring>

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

    enum class Tag : std::uint8_t {
        Nil = 0,
        Error = 1,
        String = 2,
        Int64 = 3,
        Double = 4,
        Array = 5
    };
    struct Err {
        std::uint32_t code;
        std::string msg;
    };
    struct Reply {
        std::variant<std::monostate,std::string,Err,std::int64_t,double,std::vector<Reply>> var;

        [[nodiscard]] Tag tag() const {
            return static_cast<Tag>(var.index());
        };
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

    auto encodeU32Le(std::uint32_t v) -> std::string {
        std::string out(4, '\0');
        std::memcpy(out.data(),&v,4);

        /* alternative way to store as little-endian
        std::string out{};
        for (auto i = 0; i < 4; ++i) {
            out += static_cast<char>((v >> 8*i) & 0xff);
        }*/
        return out;
    }

    auto getTagName(Tag tag) -> std::string_view {
        switch (tag) {
            case Tag::Nil:
                return "nil";
            case Tag::Error:
                return "err";
            case Tag::String:
                return "str";
            case Tag::Int64:
                return "i64";
            case Tag::Double:
                return "f64";
            case Tag::Array:
                return "arr";
            default:
                return {};
        }
    }
    auto decodeReply(std::string_view buf,std::size_t& pos, std::vector<RedisProxy::Segment>& segments,int depth = 0) -> std::optional<Reply> {

        if (depth > 8) return std::nullopt;

        auto take = [&](size_t n) -> std::optional<std::string_view> {
            if (n > buf.size() - pos) return std::nullopt;

            auto ptr = reinterpret_cast<const char*>(buf.data() +pos);
            pos += n;
            return std::string_view(ptr,n);
        };

        Tag tag;
        auto tagChunk = take(1);
        if (!tagChunk.has_value()) return std::nullopt;

        tag = static_cast<Tag>(static_cast<unsigned char>(tagChunk.value()[0]));
        segments.emplace_back(RedisProxy::Segment{
            std::format("tag {}", getTagName(tag)), std::string(tagChunk.value())
        });
        pos++;
        switch (tag) {
            case Tag::Nil:
                segments.emplace_back(RedisProxy::Segment{
                    "tag nil",std::string(*tagChunk)
                });
                return Reply{.var = std::monostate{}};
            case Tag::Error: {
                segments.emplace_back(RedisProxy::Segment{
                    "tag err",std::string(*tagChunk)
                });

                auto codeChunk = take(4);
                if (!codeChunk.has_value()) return std::nullopt;

                std::uint32_t code{};
                std::memcpy(&code, codeChunk.value().data(), 4);
                segments.emplace_back(RedisProxy::Segment
                    {std::format("code {}", code), std::string(*codeChunk)
                    });

                auto lenChunk = take(4);
                if (!lenChunk) return std::nullopt;

                std::uint32_t len{};
                std::memcpy(&len,lenChunk.value().data(),4);
                segments.emplace_back(RedisProxy::Segment
                  {std::format(",len {}", len), std::string(*lenChunk)
                  });

                auto msgChunk = take(len);
                if (!msgChunk) return std::nullopt;
                segments.emplace_back(RedisProxy::Segment
                  {"msg", std::string(*msgChunk)
                  });

                return Reply{.var = Err{.code = code, .msg = std::string(*msgChunk)}
                };
            }
            case Tag::String: {
                segments.emplace_back(RedisProxy::Segment{
                   "tag str",std::string(*tagChunk)
               });
                auto lenChunk = take(4);
                if (!lenChunk.has_value()) return std::nullopt;

                std::uint32_t len{};
                std::memcpy(&len, lenChunk.value().data(), 4);
                segments.emplace_back(RedisProxy::Segment{
                    std::format("len {}", len), std::string(*lenChunk)});

                auto stringChunk = take(7);
                if (!stringChunk.has_value()) return std::nullopt;

                std::uint64_t string{};
                std::memcpy(&string,stringChunk.value().data(),7);
                segments.emplace_back(RedisProxy::Segment{
                    std::format("{}", string), std::string(*stringChunk)});

                return Reply{.var = std::string{*stringChunk}};
            }
            case Tag::Int64: {
                segments.emplace_back(RedisProxy::Segment{
                   "tag i64",std::string(*tagChunk)
               });
                auto chunk = take(8);
                if (!chunk.has_value()) return std::nullopt;

                std::int64_t int64{};
                std::memcpy(&int64, chunk.value().data(), 8);
                segments.emplace_back(RedisProxy::Segment{
                    std::format("{}", int64), std::string(*chunk)});

                return Reply{.var = int64};
            }
            case Tag::Double: {
                segments.emplace_back(RedisProxy::Segment{
                   "tag f64",std::string(*tagChunk)
               });

                auto chunk = take(8);
                if (!chunk.has_value()) return std::nullopt;

                double d{};
                std::memcpy(&d, chunk.value().data(), 8);
                std::ostringstream oss;
                oss << d;
                segments.emplace_back(RedisProxy::Segment{
                    oss.str(), std::string(*chunk)});

                return Reply{.var = d};
            }
            case Tag::Array: {
                segments.emplace_back(RedisProxy::Segment{
                   "tag arr",std::string(*tagChunk)
               });

                auto countChunk = take(4);
                if (!countChunk.has_value()) return std::nullopt;

                std::uint32_t count{};
                std::memcpy(&count, countChunk.value().data(), 4);
                segments.emplace_back(RedisProxy::Segment{std::format("count {}", count), std::string(countChunk.value())});

                if (count > 10000) return std::nullopt;

                std::vector<Reply> replies{};
                for (std::uint32_t i = 0; i< count; ++i) {
                    auto item = decodeReply(buf,pos,segments,depth+1);
                    if (!item) return std::nullopt;
                    replies.emplace_back(std::move(*item));
                }
                return Reply{.var = replies};
            }
            default:
                return std::nullopt;
        }
    };

    auto redisCliFormatter(const Reply& r, const std::string& indent ="") -> std::string {
        switch (r.tag()) {
            default:
                throw std::runtime_error("unkown tag");
            case Tag::Nil:
                return "(nil)";
            case Tag::Error: {
                const auto&[code, msg] = std::get<Err>(r.var);
                return std::format("(error) {}",msg);
            }
            case Tag::String: {
                auto str = std::get<std::string>(r.var);
                return std::format("\"{}\"",str);
            }
            case Tag::Int64: {
                const auto i64 = std::get<std::int64_t>(r.var);
                return std::format("(integer) {}",i64);
            }
            case Tag::Double: {
                std::ostringstream oss;
                oss << ("double") << std::get<double>(r.var);
                return oss.str();
            }
            case Tag::Array:
                auto replies = std::get<std::vector<Reply>>(r.var);
                if (replies.empty()) return "(empty array)";

                std::string out;
                for (std::size_t i = 0 ; i < replies.size(); ++i) {
                    if (i>0) out += "\n" + indent;

                    std::string prefix = std::format("{}) ", i+1);
                    std::string child = indent + std::string(prefix.size(), ' ');
                    out += prefix + redisCliFormatter(replies[i],child);
                }
                return out;
        }
    }
}
