#include "staticFiles.hpp"
#include <fstream>

namespace {
   const std::unordered_map<std::string, std::string_view> mimeTable{
      {".html", "text/html; charset=utf-8"},
      {".css", "text/css; charset=utf-8"},
      {".txt", "text/plain; charset=utf-8"},
      {".js", "text/javascript; charset=utf-8"},
      {".json", "application/json"},
      {".svg", "image/svg+xml"},
      {".png", "image/png"},
      {".jpg", "image/jpeg"},
      {".jpeg", "image/jpeg"},
      {".ico", "image/x-icon"},
      {".pdf", "application/pdf"}
   };

   auto mimeType(const std::filesystem::path& p) -> std::string_view {
      if (const auto it = mimeTable.find(p.extension());
         it != mimeTable.cend()) return it->second;
      return "application/octet-stream";
   }
   auto safeRelative(std::string_view rel) -> bool {
      if (rel.empty() || rel.starts_with('/') || rel.find('\0') != std::string_view::npos)
         return false;
      for (std::filesystem::path path{rel}; const auto& p : path) {
         if (p == "..") return false;
      }
      return true;
   }
};

auto staticFiles(std::filesystem::path dir, std::string urlPrefix) -> Handler {
   return [dir,urlPrefix](const Request& req,const std::string&)-> asio::awaitable<Response> {
      std::string_view sv = req.target();
      sv = sv.substr(0,sv.find('?'));
      sv = sv.substr(urlPrefix.size());

      std::filesystem::path path = dir / sv;
      if (!safeRelative(sv) || !is_regular_file(path))
         co_return makeResponse(http::status::not_found, "404 not found\n","text/plain");

      std::ifstream file(path,std::ios::binary);
      if (!file)
         co_return makeResponse(http::status::not_found, "404 not found\n","text/plain");


      std::string body(std::istreambuf_iterator<char>(file), {});
      auto res = makeResponse(http::status::ok,std::move(body),mimeType(path));
      res.set(http::field::cache_control, "no-cache");
      co_return res;
   };
}