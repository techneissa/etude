// SPDX-License-Identifier: GPL-3.0-or-later
#include "net_fetch.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace etude::app {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool starts_with_https(std::string_view url) {
    return url.size() > 8 && lower(url.substr(0, 8)) == "https://";
}

// Waits until `kFetchSpacing` has passed since the last request to `host`, then notes this one.
void pace(const std::string& host) {
    static std::mutex mutex;
    static std::map<std::string, std::chrono::steady_clock::time_point> last;
    std::chrono::steady_clock::time_point at;
    {
        const std::lock_guard lock(mutex);
        const auto now = std::chrono::steady_clock::now();
        auto& t = last[host];
        at = std::max(now, t + kFetchSpacing);
        t = at;
    }
    std::this_thread::sleep_until(at);
}

std::string_view curl_reason(CURLcode code) {
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
        return "No connection";
    case CURLE_OPERATION_TIMEDOUT:
        return "The site took too long";
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CACERT_BADFILE:
        return "The site's certificate could not be checked";
    case CURLE_FILESIZE_EXCEEDED:
        return "The file is too large";
    case CURLE_UNSUPPORTED_PROTOCOL:
        return "Only secure links are used";
    default:
        return "The download failed";
    }
}

struct Transfer {
    std::string* body = nullptr;
    std::uint64_t max_bytes = 0;
    bool too_large = false;
    const FetchProgress* progress = nullptr;
    bool cancelled = false;
};

std::size_t on_write(char* data, std::size_t size, std::size_t count, void* user) {
    auto* t = static_cast<Transfer*>(user);
    const std::size_t n = size * count;
    if (t->body->size() + n > t->max_bytes) {
        t->too_large = true;
        return 0;
    }
    t->body->append(data, n);
    return n;
}

int on_progress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* t = static_cast<Transfer*>(user);
    if (t->progress && *t->progress &&
        !(*t->progress)(static_cast<std::uint64_t>(std::max<curl_off_t>(now, 0)),
                        static_cast<std::uint64_t>(std::max<curl_off_t>(total, 0)))) {
        t->cancelled = true;
        return 1;
    }
    return 0;
}

} // namespace

std::string_view fetch_user_agent() {
    return "etude/0.1 (piano practice app; free song catalogue downloads)";
}

std::string https_host(std::string_view url) {
    if (!starts_with_https(url)) {
        return {};
    }
    std::string_view rest = url.substr(8);
    rest = rest.substr(0, rest.find_first_of("/?#"));
    if (rest.empty() || rest.find('@') != std::string_view::npos) {
        return {};
    }
    if (const std::size_t colon = rest.find(':'); colon != std::string_view::npos) {
        if (rest.substr(colon + 1) != "443") {
            return {};
        }
        rest = rest.substr(0, colon);
    }
    return lower(rest);
}

bool fetch_allowed(std::string_view url, std::span<const std::string_view> hosts) {
    const std::string host = https_host(url);
    if (host.empty()) {
        return false;
    }
    return std::any_of(hosts.begin(), hosts.end(),
                       [&](std::string_view h) { return lower(h) == host; });
}

std::string redirect_target(std::string_view from, std::string_view location) {
    if (location.find("://") != std::string_view::npos) {
        return std::string(location);
    }
    if (location.empty() || location[0] != '/' || (location.size() > 1 && location[1] == '/')) {
        return {};
    }
    const std::size_t scheme = from.find("://");
    if (scheme == std::string_view::npos) {
        return {};
    }
    const std::size_t path = from.find('/', scheme + 3);
    return std::string(from.substr(0, path)) + std::string(location);
}

std::string_view http_status_reason(int status) {
    if (status == 404 || status == 410) {
        return "Not found on the site";
    }
    if (status == 429 || status == 503) {
        return "The site is busy; try again later";
    }
    if (status == 401 || status == 403 || status == 418) {
        return "The site refused the download";
    }
    if (status >= 500) {
        return "The site had a problem";
    }
    return "The download failed";
}

namespace {

// The options of one hop: https only, no redirect followed by curl (the loop checks each hop), the
// limits, and the transfer's callbacks.
void configure_transfer(CURL* curl, const std::string& at, Transfer& t, std::uint64_t max_bytes) {
    curl_easy_setopt(curl, CURLOPT_URL, at.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     static_cast<long>(std::chrono::milliseconds(kFetchConnectLimit).count()));
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(kFetchStallLimit.count()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                     static_cast<long>(std::chrono::milliseconds(kFetchTotalLimit).count()));
    curl_easy_setopt(curl, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(max_bytes));
    const std::string agent(fetch_user_agent());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &t);
}

} // namespace

CurlFetcher::CurlFetcher() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

CurlFetcher::~CurlFetcher() = default;

FetchResult CurlFetcher::fetch(std::string_view url, std::span<const std::string_view> hosts,
                               std::uint64_t max_bytes, const FetchProgress& progress) {
    FetchResult r;
    std::string at(url);
    for (int hop = 0; hop <= kFetchMaxRedirects; ++hop) {
        if (!fetch_allowed(at, hosts)) {
            r.reason = starts_with_https(at) ? "That site is not one of the catalogues"
                                             : "Only secure links are used";
            return r;
        }
        pace(https_host(at));
        CURL* curl = curl_easy_init();
        if (!curl) {
            r.reason = "The download failed";
            return r;
        }
        r.body.clear();
        Transfer t{&r.body, max_bytes, false, &progress, false};
        configure_transfer(curl, at, t, max_bytes);
        const CURLcode code = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        r.status = static_cast<int>(status);
        double elapsed = 0.0;
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);
        char* next = nullptr;
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &next);
        const std::string location = next ? next : "";
        curl_easy_cleanup(curl);
        if (t.cancelled) {
            r.cancelled = true;
            r.body.clear();
            r.reason = "Cancelled";
            return r;
        }
        if (t.too_large) {
            r.body.clear();
            r.reason = "The file is too large";
            return r;
        }
        if (code != CURLE_OK) {
            r.body.clear();
            r.reason = std::string(curl_reason(code));
            if (code == CURLE_OPERATION_TIMEDOUT) {
                r.detail = "timed out after " + std::to_string(static_cast<int>(elapsed + 0.5)) + " s";
            } else {
                r.detail = curl_easy_strerror(code);
            }
            return r;
        }
        if (status >= 300 && status < 400 && !location.empty()) {
            at = redirect_target(at, location);
            continue;
        }
        if (status != 200) {
            r.body.clear();
            r.reason = std::string(http_status_reason(r.status));
            return r;
        }
        r.ok = true;
        r.final_url = at;
        return r;
    }
    r.body.clear();
    r.reason = "The site sent the download round in circles";
    return r;
}

FolderFetcher::FolderFetcher(std::string folder, std::uint64_t bytes_per_s, bool fail_first)
    : folder_(std::move(folder)), bytes_per_s_(bytes_per_s), fail_first_(fail_first) {}

FetchResult FolderFetcher::fetch(std::string_view url, std::span<const std::string_view> hosts,
                                 std::uint64_t max_bytes, const FetchProgress& progress) {
    ++fetches_;
    FetchResult r;
    if (!fetch_allowed(url, hosts)) {
        r.reason = starts_with_https(url) ? "That site is not one of the catalogues"
                                          : "Only secure links are used";
        return r;
    }
    if (fail_first_) {
        const std::string line = std::string(url) + "\n";
        if (failed_.find(line) == std::string::npos) {
            failed_ += line;
            r.reason = "No connection";
            return r;
        }
    }
    std::string_view rest = url.substr(8);
    rest = rest.substr(0, rest.find_first_of("?#"));
    const std::filesystem::path file = std::filesystem::path(folder_) / std::string(rest);
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        r.status = 404;
        r.reason = std::string(http_status_reason(404));
        return r;
    }
    std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (all.size() > max_bytes) {
        r.reason = "The file is too large";
        return r;
    }
    const std::uint64_t total = all.size();
    constexpr int kSteps = 20;
    const std::string_view path = rest;
    const bool song = path.size() > 4 && (path.substr(path.size() - 4) == ".mid" ||
                                          path.substr(path.size() - 5) == ".mscx");
    if (song && hold_.load()) {
        while (hold_.load()) {
            if (progress && !progress(total / 2, total)) {
                r.cancelled = true;
                r.reason = "Cancelled";
                return r;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (bytes_per_s_ > 0 && song) {
        const auto step = std::chrono::microseconds(
            static_cast<std::int64_t>(1e6 * static_cast<double>(total) /
                                      static_cast<double>(bytes_per_s_) / kSteps));
        for (int i = 1; i <= kSteps; ++i) {
            std::this_thread::sleep_for(step);
            if (progress && !progress(total * static_cast<std::uint64_t>(i) / kSteps, total)) {
                r.cancelled = true;
                r.reason = "Cancelled";
                return r;
            }
        }
    } else if (progress && !progress(total, total)) {
        r.cancelled = true;
        r.reason = "Cancelled";
        return r;
    }
    r.ok = true;
    r.status = 200;
    r.body = std::move(all);
    r.final_url = std::string(url);
    return r;
}

} // namespace etude::app
