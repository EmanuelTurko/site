#include "page.hpp"

#include "util/fileio.hpp"

auto Page::load() const -> std::shared_ptr<const Snapshot> {
    auto v = readJson(_root / "content" / "site.json");
    mustache::TemplateSet ts(_root / "templates");

    return std::make_shared<Snapshot>(std::move(ts), std::move(v));
}

auto Page::snapshot() -> std::shared_ptr<const Snapshot> {
    std::lock_guard<std::mutex> lg(_mu); // lg's destructor unlocks _mu
    if (_reload) _current = load();
    return _current; // increases the refcount
}

auto Page::render(const boost::json::object &runtime) -> std::string {

    auto start = std::chrono::steady_clock::now();
    auto snap = snapshot();
    auto obj = snap->content.as_object();
    for (const auto& r : runtime) {
        obj[r.key()] = r.value();
    }
    auto html = snap->templates.render("index.html",std::move(obj));

    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration<double,std::milli>(now - start).count();
    auto timing = std::format("{:.2f}",ms);
    std::string render = "__RENDER_US__";

    if (auto it = html.find(render);
        it != std::string::npos)
        html.replace(it,render.size(),timing);

    return html;
}
