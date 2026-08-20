// Minimal LaTeX to SVG helper that relies on a MicroTeX binary.
#include "panim/LatexRenderer.hpp"
#include "panim/Log.hpp"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace panim {

    namespace {

        std::string find_in_path(const std::string &name) {
            const char *path_env = std::getenv("PATH");
            if (!path_env) {
                return "";
            }

            std::string path(path_env);
            size_t start = 0;
            while (true) {
                size_t end = path.find(':', start);
                std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
                if (!dir.empty()) {
                    std::filesystem::path candidate = std::filesystem::path(dir) / name;
                    if (::access(candidate.c_str(), X_OK) == 0) {
                        return candidate.string();
                    }
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            return "";
        }

        std::string shell_quote(std::string_view value) {
            std::string quoted("'");
            for (const char ch : value) {
                if (ch == '\'') {
                    quoted += "'\\''";
                } else {
                    quoted += ch;
                }
            }
            quoted += '\'';
            return quoted;
        }

    } // namespace

    LatexRenderer::LatexRenderer(std::filesystem::path scratch_dir) : scratch_dir_(std::move(scratch_dir)) {
        std::error_code ec;
        std::filesystem::create_directories(scratch_dir_, ec);
        if (ec) {
            last_error_ = "Failed to create scratch directory: " + scratch_dir_.string();
            PANIM_LOG_ERROR(last_error_);
            available_ = false;
            return;
        }

        std::string bin;
        Status st = locate_microtex(bin);
        if (!st.ok) {
            last_error_ = st.message;
            PANIM_LOG_ERROR(last_error_);
            available_ = false;
            return;
        }
        microtex_bin_ = std::move(bin);
        available_ = true;
        PANIM_LOG_INFO("Using MicroTeX binary: {}", microtex_bin_);
    }

    Status LatexRenderer::render_svg(const std::string &latex, std::filesystem::path &out_path) {
        if (!available_) {
            return Status::failure("MicroTeX unavailable: " + last_error_);
        }

        std::hash<std::string> hasher;
        auto filename = std::to_string(hasher(latex)) + std::string(".svg");
        out_path = scratch_dir_ / filename;

        if (std::filesystem::exists(out_path)) {
            return Status::success();
        }

        const std::string command = shell_quote(microtex_bin_) + " -headless" + " " + shell_quote("-foreground=#ffffffff") + " " +
                                    shell_quote("-background=#00000000") + " " + shell_quote("-padding=4") + " " + shell_quote("-input=" + latex) +
                                    " " + shell_quote("-output=" + out_path.string()) + " >/dev/null 2>&1";
        int result = std::system(command.c_str());
        if (result != 0) {
            PANIM_LOG_ERROR("MicroTeX failed (code {}): {}", result, command);
            return Status::failure("MicroTeX invocation failed");
        }

        return Status::success();
    }

    Status LatexRenderer::locate_microtex(std::string &out_path) const {
        const char *env_bin = std::getenv("MICROTEX_BIN");
        if (env_bin && ::access(env_bin, X_OK) == 0) {
            out_path = env_bin;
            return Status::success();
        }

        const char *env_root = std::getenv("MICROTEX_ROOT");
        if (env_root) {
            std::filesystem::path candidate = std::filesystem::path(env_root) / "build/LaTeX";
            if (::access(candidate.c_str(), X_OK) == 0) {
                out_path = candidate.string();
                return Status::success();
            }
        }

        std::string from_path = find_in_path("microtex");
        if (!from_path.empty()) {
            out_path = from_path;
            return Status::success();
        }

        return Status::failure("MicroTeX binary not found. Install 'microtex' on PATH or set MICROTEX_BIN.");
    }

} // namespace panim
