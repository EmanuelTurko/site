#include "playground.hpp"

Playground::Playground(Config cfg) : _cfg(std::move(cfg)),
                                     _pool(_cfg.workers) {
    std::error_code ec{};
    if (!_cfg.compiler.empty()) {
        auto path = std::filesystem::canonical(_cfg.compiler, ec);
        if (ec) {
            logging::warn(ec.message() + _cfg.compiler);
            _cfg.compiler.clear();
            return;
        }
        if (access(path.c_str(),X_OK) != 0) {
            logging::warn(strerror(errno) + path.string());
            _cfg.compiler.clear();
            return;
        }
        _cfg.compiler = path;
    }
}

Playground::~Playground() {
    _pool.join();
}
