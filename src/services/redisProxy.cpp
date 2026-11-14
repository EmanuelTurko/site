#include "redisProxy.hpp"
#include <cstring>

#include "util/logging.hpp"

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

auto RedisProxy::roundTrip(beast::tcp_stream &stream, const std::vector<std::string> &args,
                           std::vector<Segment> *sent) -> asio::awaitable<std::optional<std::string> > {
    stream.expires_after(std::chrono::seconds(1)); // 1 second deadline for read and write

    std::vector<std::uint8_t> payload{};

    // Append encoded bytes to payload and record them in sent.
    auto addBytes = [&](const std::string &bytes, const std::string &label) {
        payload.insert(
            payload.end(),
            bytes.begin(),
            bytes.end());

        if (sent != nullptr)
            sent->push_back({label, bytes});
    };

    // Append an argument's actual string bytes.
    auto addString = [&](const std::string &str) {
        payload.insert(
            payload.end(),
            reinterpret_cast<const std::uint8_t *>(str.data()),
            reinterpret_cast<const std::uint8_t *>(str.data() + str.size()));

        if (sent != nullptr)
            sent->push_back({"\"" + str + "\"", str});
    };

    // Calculate body length:
    // 4 bytes for argument count + 4 bytes per argument length + argument bytes.
    std::uint32_t body = 4;

    for (const auto &arg: args)
        body += 4 + static_cast<std::uint32_t>(arg.size());

    // Append total body length.
    auto bodyBytes = encodeU32Le(body);
    addBytes(bodyBytes, "len " + std::to_string(body));

    // Append number of arguments.
    auto countBytes = encodeU32Le(static_cast<std::uint32_t>(args.size()));
    addBytes(countBytes, "nstr " + std::to_string(args.size()));

    // Append each argument's length and contents.
    for (const auto &arg: args) {
        auto lenBytes = encodeU32Le(static_cast<std::uint32_t>(arg.size()));

        addBytes(lenBytes, "len " + std::to_string(arg.size()));
        addString(arg);
    }

    // Send the complete request.
    auto wRes = co_await asio::async_write(
        stream,
        asio::buffer(payload),
        asio::as_tuple(asio::use_awaitable));

    if (auto ec = std::get<0>(wRes)) {
        if (ec == asio::error::timed_out)
            logging::error("Write timed out");

        co_return std::nullopt;
    }

    // Read the 4-byte reply length.
    std::uint8_t header[4];

    auto hRes = co_await asio::async_read(
        stream,
        asio::buffer(header),
        asio::as_tuple(asio::use_awaitable));

    if (auto ec = std::get<0>(hRes)) {
        if (ec == asio::error::timed_out)
            logging::error("Header timed out");

        co_return std::nullopt;
    }

    // Decode the little-endian reply length.
    auto headerLen =
            static_cast<std::uint32_t>(header[0]) |
            (static_cast<std::uint32_t>(header[1]) << 8) |
            (static_cast<std::uint32_t>(header[2]) << 16) |
            (static_cast<std::uint32_t>(header[3]) << 24);

    constexpr std::uint32_t maxPayloadSize = 64 * 1024;

    if (headerLen > maxPayloadSize)
        co_return std::nullopt;

    // Read the reply body.
    std::string s(headerLen, '\0');

    auto rRes = co_await asio::async_read(
        stream,
        asio::buffer(s),
        asio::as_tuple(asio::use_awaitable));

    if (auto ec = std::get<0>(rRes)) {
        if (ec == asio::error::timed_out)
            logging::error("Read timed out");

        co_return std::nullopt;
    }

    co_return s;
}

auto RedisProxy::handle(const Request &req, const RequestContext &ctx) -> asio::awaitable<Response> {
    if (_cfg.host.empty())
        co_return jsonError(http::status::service_unavailable, "redis demo is offline");

    json::value value{};
    {
        boost::system::error_code ec{};
        value = json::parse(req.body(), ec);

        if (ec || !value.is_object())
            co_return jsonError(
                http::status::bad_request, "expected {\"session\": \"...\", \"line\": \"...\"}");
    }

    json::value session{};
    std::vector<std::string> args{};
    std::string prefix{};

    std::string* command{};
    std::string* key{};
    {
        session = value.as_object()["session"];
        if (!session.is_string || !isValidSession(session.as_string()))
            co_return jsonError(http::status::bad_request, "bad session id");

        if (!_limiter.allow(ctx.remoteIp))
            co_return jsonError(http::status::too_many_requests, "slow down a little");

        auto line = value.as_object()["line"];
        args = splitIntoArgs(line.as_string());
        if (args.empty())
            co_return jsonError(http::status::bad_request, "empty command");

        command = &args[0];
        std::ranges::transform(*command, command->begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });

        auto it = kCommands.find(*command);
        if (it == kCommands.cend())
            co_return jsonError(http::status::bad_request,
                                "unknown command; try: set get del keys pexpire pttl zadd zrem zscore zquery");

        if (it->second.argc != args.size())
            co_return jsonError(http::status::bad_request,
                                std::format("'{}' takes {} argument(s)", *command, it->second.argc - 1));

        for (const auto arg: args) {
            if (arg.size() > 64)
                co_return jsonError(http::status::bad_request, "arguments are limited to 64 bytes");
        }

        if (*command == "pexpire") {
            if (std::stoi(args.back()) < 0)
                co_return jsonError(http::status::bad_request, "removing a TTL is disabled on the public demo");
            if (std::stoi(args.back()) > 600000) args.back() = "600000";
        }

        prefix = std::format("v:{}:", session.get_string());
        if (it->second.hasKey) {
            key = &args[1];
            *key = prefix + *key;
        }
    }

    beast::tcp_stream stream(co_await asio::this_coro::executor);
    {
        tcp::endpoint endpoint{};
        try {
            endpoint.address(asio::ip::make_address(_cfg.host));
            endpoint.port(_cfg.port);
        } catch (const std::exception &) {
            co_return jsonError(http::status::internal_server_error, "bad redis host in config");
        }

        stream.expires_after(std::chrono::seconds(1));

        auto r = co_await stream.async_connect(endpoint, asio::as_tuple(asio::use_awaitable));
        if (auto ec = std::get<0>(r)) {
            if (ec == asio::error::connection_refused)
                co_return jsonError(http::status::service_unavailable, "connection refused");
            if (ec == asio::error::timed_out)
                co_return jsonError(http::status::service_unavailable, "timed out");
            co_return jsonError(http::status::service_unavailable, "redis server unreachable");
        }
    }

    long timeUs{};
    std::optional<std::string> reply{};
    std::vector<Segment> sent;
    {
        auto start = std::chrono::steady_clock::now();
        reply = co_await roundTrip(stream, args, &sent);
        if (!reply)
            co_return jsonError(http::status::bad_gateway, "no reply from redis server");

        auto now = std::chrono::steady_clock::now();
        timeUs = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    }

    std::vector<Segment> segments{};
    std::optional<Reply> recv{};
    {
        size_t pos{};
        recv = decodeReply(*reply, pos, segments);
        if (!recv)
            co_return jsonError(http::status::bad_gateway, "could not decode reply");

        if (auto replies = std::get_if<std::vector<Reply> >(&recv->var); *command == "keys" && replies) {
            for (auto it = replies->begin(); it != replies->end();) {
                if (auto str = std::get_if<std::string>(&it->var)) {
                    if (str->starts_with(prefix)) {
                        *str = str->substr(prefix.size());
                        ++it;
                    } else it = replies->erase(it);
                } else it = replies->erase(it);
            }

            segments.clear();
            segments.push_back({std::format("({} bytes, other visitors' keys hidden)", reply->size())});
        }
    }

    {
        if (*command == "set" || *command == "zadd") {
            std::vector<std::string> ttlArgs = {"pttl", *key};
            auto ttlReply = co_await roundTrip(stream, ttlArgs, nullptr);
            if (!ttlReply) co_return jsonError(http::status::bad_gateway, "no reply from redis server");

            size_t ttlPos{};
            std::vector<Segment> ttlSegments;
            auto ttlDecoded = decodeReply(*ttlReply, ttlPos, ttlSegments);
            if (!ttlDecoded) co_return jsonError(http::status::bad_gateway, "could not decode ttl-reply");

            if (ttlDecoded->tag() == Tag::Int64) {
                if (auto i64 = std::get<std::int64_t>(ttlDecoded->var); i64 == -1) {
                    ttlArgs = {"pexpire", *key, "600000"};
                    auto expireReply = co_await roundTrip(stream, ttlArgs, nullptr);
                    if (!expireReply)
                        co_return jsonError(http::status::bad_gateway, "no reply from redis server");
                }
            }
        }
    }

    json::object result;
    {
        result["ok"] = true;
        result["text"] = *reply;
        result["us"] = timeUs;
        result["sent"] = segmentsToJson(sent);
        result["recv"] = segmentsToJson(segments);
        result["prefix"] = prefix;
    }
    co_return jsonResponse(http::status::ok, result);
}
