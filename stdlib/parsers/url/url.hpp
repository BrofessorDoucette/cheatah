// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

// cheatah::parsers::url — a from-scratch parser for the http(s) URL subset the requests module
// speaks: `scheme://host[:port][/path][?query]`. No allocation beyond the component strings, no
// regex, no dependencies. Userinfo (`user@`) and fragments (`#...`) are not supported — the first
// is an obsolete security hazard in http URLs, the second is never sent to the server anyway.
//
// Laid out as a cheatah stdlib module: from .purr this is `import parsers.url.Parser as Parser`,
// mirroring `import parsers.json.Parser as Parser` — each parsers submodule exposes a Parser.

#include <string>
#include <string_view>

namespace cheatah::parsers::url {

/**
 * @brief One parsed http(s) URL. @c target is the HTTP request-target — the path plus the original
 *        query, always beginning with '/' (an empty path becomes "/").
 */
struct Url {
    std::string scheme;  ///< the lowercased scheme: "http" or "https".
    std::string host;    ///< the host (name or IP); never empty on success.
    long long port = 0;  ///< the explicit port, or the scheme default (80 for http, 443 for https).
    std::string target;  ///< the HTTP request-target "/path?query" (always begins with '/').
};

/**
 * @brief The URL parser. Stateless and reusable; a class (not a free function) so the module
 *        surface is symmetric with parsers::json::Parser and imports the same way from cheatah.
 */
class Parser {
public:
    /**
     * Parse @p text into @p out. Accepts `scheme://host[:port][/path][?query]` with scheme http or
     * https (case-insensitive). Rejects empty hosts, non-numeric or out-of-range ports, userinfo,
     * and fragments. On failure @p out is left unspecified.
     *
     * @param text the URL text to parse.
     * @param out receives the parsed components on success.
     * @return true iff @p text is a valid accepted URL.
     * @complexity O(|text|)
     * @alloc the component strings in @p out
     * @test UrlParser.Components
     */
    [[nodiscard]] bool parse(std::string_view text, Url& out) const {  // NOLINT(readability-convert-member-functions-to-static): callers hold a Parser instance (the .purr API shape)
        const std::size_t scheme_end = text.find("://");
        if (scheme_end == std::string_view::npos || scheme_end == 0) {
            return false;
        }
        out.scheme.clear();
        for (const char ch : text.substr(0, scheme_end)) {  // lowercase the scheme as we copy
            out.scheme.push_back(ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
        }
        if (out.scheme != "http" && out.scheme != "https") {
            return false;
        }

        std::string_view rest = text.substr(scheme_end + 3);
        const std::size_t path_start = rest.find('/');
        const std::size_t query_start = rest.find('?');
        const std::size_t authority_end = std::min(path_start, query_start);
        const std::string_view authority = rest.substr(0, authority_end);
        if (authority.empty() || authority.find('@') != std::string_view::npos ||
            rest.find('#') != std::string_view::npos) {
            return false;  // empty host, userinfo, and fragments are all rejected
        }

        const std::size_t colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            out.host = std::string(authority);
            out.port = (out.scheme == "https") ? 443 : 80;
        } else {
            out.host = std::string(authority.substr(0, colon));
            const std::string_view digits = authority.substr(colon + 1);
            if (out.host.empty() || digits.empty() || digits.size() > 5) {
                return false;
            }
            long long port = 0;
            for (const char ch : digits) {
                if (ch < '0' || ch > '9') {
                    return false;
                }
                port = port * 10 + (ch - '0');
            }
            if (port < 1 || port > 65535) {
                return false;
            }
            out.port = port;
        }

        // The request-target: everything from the first '/' on (or "/" when the path is absent,
        // including the bare-query form "host?x=1" -> "/?x=1").
        if (path_start != std::string_view::npos) {
            out.target = std::string(rest.substr(path_start));
        } else if (query_start != std::string_view::npos) {
            out.target = "/" + std::string(rest.substr(query_start));
        } else {
            out.target = "/";
        }
        return true;
    }
};

}  // namespace cheatah::parsers::url
