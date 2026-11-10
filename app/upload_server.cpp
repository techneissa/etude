// SPDX-License-Identifier: GPL-3.0-or-later
#include "upload_server.hpp"
#include "crash_thread.hpp"

#include "upload_api.hpp"
#include "upload_connections.hpp"
#include "upload_page.hpp"

#include "etude/upload_network.hpp"
#include "etude/upload_rules.hpp"

#include <httplib.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <utility>

namespace etude::app {

namespace {

// Worker threads for requests: a phone sends a file at a time, and a Pi has four cores.
constexpr std::size_t kWorkers = 4;
// An upload's body is its file plus the multipart framing; anything else is a short form.
constexpr std::size_t kBodyOverhead = 1024 * 1024;
constexpr std::size_t kMaxFormBytes = 4096;
// A client that stops sending for this long loses its connection (the library counts it per read;
// the connection's total time is ConnectionLimiter's).
constexpr time_t kIoTimeoutS = 10;
// Connections waiting for a worker: past this the next one is closed at once.
constexpr std::size_t kQueued = 16;

UploadMs steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void reply(httplib::Response& res, const ApiReply& r) {
    res.status = r.status;
    res.set_content(r.body, "application/json; charset=utf-8");
    if (!r.set_cookie.empty()) {
        res.set_header("Set-Cookie", r.set_cookie);
    }
}

bool is_public(const httplib::Request& req) {
    return (req.method == "GET" && (req.path == "/" || req.path == "/api/session")) ||
           (req.method == "POST" && req.path == "/api/login");
}

} // namespace

struct UploadServer::Impl {
    explicit Impl(UploadServerOptions opts)
        : options(std::move(opts)),
          limiter(ConnectionLimiter::Options{options.max_per_peer, options.request_deadline}),
          api(UploadApi::Options{options.songs, options.removed, steady_ms,
                                 [this] {
                                     const std::lock_guard<std::mutex> lock(random_mutex);
                                     return static_cast<std::uint32_t>(random());
                                 },
                                 options.open_song, options.song_length}) {}

    // One listener's settings, handlers and guard (every listener has its own copy).
    void configure(httplib::Server& server) {
        server.set_read_timeout(kIoTimeoutS);
        server.set_write_timeout(kIoTimeoutS);
        // One request a connection: a kept-open connection would hold a worker for its timeout.
        server.set_keep_alive_max_count(1);
        // Listen on the port alone: the library's default also sets SO_REUSEPORT, which lets a
        // second process (or a second copy of this one) bind the same port and share the clients.
        server.set_socket_options(
            [](socket_t sock) { httplib::set_socket_opt(sock, SOL_SOCKET, SO_REUSEADDR, 1); });
        server.set_payload_max_length(static_cast<std::size_t>(kMaxUploadBytes) + kBodyOverhead);
        // Four workers and a short queue; a connection past that is closed, not kept waiting.
        server.new_task_queue = [] { return new httplib::ThreadPool(kWorkers, kWorkers, kQueued); };
        server.set_default_headers({{"Cache-Control", "no-store"},
                                    {"X-Content-Type-Options", "nosniff"},
                                    {"X-Frame-Options", "DENY"},
                                    {"Referrer-Policy", "no-referrer"},
                                    {"Content-Security-Policy",
                                     "default-src 'self'; style-src 'self' 'unsafe-inline'; "
                                     "script-src 'self' 'unsafe-inline'; frame-ancestors 'none'"}});
        server.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            return guard(req, res);
        });
        // Whatever the library refuses on its own (a header too long, a body that does not parse)
        // answers in the same JSON as everything else.
        server.set_error_handler([](const httplib::Request&, httplib::Response& res) {
            if (res.body.empty()) {
                res.set_content("{\"error\":\"That request was not understood\"}",
                                "application/json; charset=utf-8");
            }
        });
        server.set_exception_handler(
            [](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
                res.status = 500;
                res.set_content("{\"error\":\"Something went wrong\"}",
                                "application/json; charset=utf-8");
            });
        routes(server);
    }

    // Everything a request must pass before its handler: the network it came from, the names it
    // used, the site it came from, the size of what it sends and a signed-in session.
    httplib::Server::HandlerResponse guard(const httplib::Request& req, httplib::Response& res) {
        const auto stop = [&](int status, const char* message) {
            reply(res, UploadApi::refused({status, message}));
            return httplib::Server::HandlerResponse::Handled;
        };
        std::array<std::uint8_t, 16> address{};
        std::size_t length = 0;
        if (!parse_address(req.remote_addr, address.data(), length) ||
            !is_local_address({address.data(), length})) {
            return stop(403, "This is only open on your home network");
        }
        const std::string host = req.get_header_value("Host");
        if (!host_header_ok(host, names)) {
            return stop(400, "Use the address shown on the Pi's screen");
        }
        if (req.method != "GET" && req.method != "POST") {
            return stop(405, "Not allowed");
        }
        if (req.method == "POST") {
            if (!origin_ok(req.get_header_value("Origin"), host)) {
                return stop(403, "That page is not this one");
            }
            // A chunked body has no length to check: it would be read up to the payload limit
            // whatever the route, so every POST says how long it is.
            if (req.has_header("Transfer-Encoding")) {
                return stop(411, "Send a Content-Length");
            }
            const std::size_t limit =
                req.path == "/api/upload"
                    ? static_cast<std::size_t>(kMaxUploadBytes) + kBodyOverhead
                    : kMaxFormBytes;
            if (req.get_header_value_u64("Content-Length") > limit) {
                return stop(413, "Too much data");
            }
        }
        if (!is_public(req) && !api.authorize(req.get_header_value("Cookie"))) {
            return stop(401, "Enter the PIN from the Pi's screen");
        }
        return httplib::Server::HandlerResponse::Unhandled;
    }

    void routes(httplib::Server& server) {
        server.Get("/", [](const httplib::Request&, httplib::Response& res) {
            const std::string_view page = upload_page_html();
            res.set_content(page.data(), page.size(), "text/html; charset=utf-8");
        });
        server.Get("/api/session", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.session(req.get_header_value("Cookie")));
        });
        server.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.login(req.get_param_value("pin")));
        });
        server.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.logout(req.get_header_value("Cookie")));
        });
        server.Get("/api/files", [this](const httplib::Request&, httplib::Response& res) {
            reply(res, api.files());
        });
        server.Post("/api/rename", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.rename(req.get_param_value("path"), req.get_param_value("name")));
        });
        server.Post("/api/move", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.move(req.get_param_value("path"), req.get_param_value("folder")));
        });
        server.Post("/api/delete", [this](const httplib::Request& req, httplib::Response& res) {
            reply(res, api.remove(req.get_param_value("path")));
        });
        server.Post("/api/upload",
                    [this](const httplib::Request& req, httplib::Response& res,
                           const httplib::ContentReader& reader) { upload(req, res, reader); });
    }

    // One file per request (the page sends them one at a time, which also gives it a progress bar
    // per file). The first part with a file name is stored; its bytes go to disk as they arrive.
    void upload(const httplib::Request& req, httplib::Response& res,
                const httplib::ContentReader& reader) {
        if (!req.is_multipart_form_data()) {
            reply(res, UploadApi::refused({400, "Send the file as a form"}));
            return;
        }
        struct Transfer {
            UploadApi& api;
            explicit Transfer(UploadApi& a) : api(a) { api.transfer_begin(); }
            ~Transfer() {
                api.progress_idle(); // when a thrown error skipped progress_end
                api.transfer_end();
            }
        } transfer(api);
        std::unique_ptr<UploadSink> sink;
        std::string name;
        ServiceError error;
        bool had_file = false;
        bool taking = false; // the part being read is the file
        std::size_t ignored = 0;
        const bool read = reader(
            [&](const httplib::FormData& part) {
                taking = false;
                if (part.filename.empty()) {
                    return true; // a text field: its bytes are skipped
                }
                if (had_file) {
                    ++ignored; // a second file: skipped, and said so
                    return true;
                }
                had_file = true;
                name = part.filename;
                api.progress_begin(name, req.get_header_value_u64("Content-Length"));
                sink = api.begin_upload(name, &error);
                taking = sink != nullptr;
                return taking;
            },
            [&](const char* data, std::size_t size) {
                if (!taking || !sink->write(data, size)) {
                    return !taking;
                }
                api.progress_add(size);
                return true;
            });
        if (!had_file) {
            reply(res, UploadApi::refused({400, "No file was sent"}));
            return;
        }
        bool stored = false;
        if (sink == nullptr) {
            reply(res, UploadApi::refused(error));
        } else if (!read && !sink->error().message.empty()) {
            reply(res, UploadApi::refused(sink->error()));
        } else if (!read || !sink->finish()) {
            reply(res, UploadApi::refused(sink->error().message.empty()
                                              ? ServiceError{400, "The upload stopped"}
                                              : sink->error()));
        } else {
            stored = true;
            reply(res, UploadApi::uploaded(name, sink->saved_as(), ignored));
        }
        api.progress_end(stored);
    }

    void watch() {
        std::unique_lock<std::mutex> lock(watch_mutex);
        while (!watch_stop) {
            watch_cv.wait_for(lock, options.sweep_interval, [this] { return watch_stop; });
            if (watch_stop) {
                break;
            }
            if (closing) {
                limiter.close_all(); // a late connection of a run that is ending
            } else {
                limiter.sweep();
            }
            if (api.tick()) {
                end_connections(); // idle for ten minutes
            }
        }
    }

    // The run is ending: the listeners close, then every connection that is still open, so the
    // threads that serve them (and the ones that join them) are not held by a client that is idle
    // or slow until the library's own read timeout. The watchdog keeps closing what comes in late.
    void end_connections() {
        closing = true;
        stop_servers();
        limiter.close_all();
    }

    void stop_servers() {
        const std::lock_guard<std::mutex> lock(servers_mutex);
        for (const auto& server : servers) {
            server->stop();
        }
    }

    bool any_running() {
        const std::lock_guard<std::mutex> lock(servers_mutex);
        for (const auto& server : servers) {
            if (server->is_running()) {
                return true;
            }
        }
        return false;
    }

    // Joins the threads of a run that has ended (stopped, or turned itself off).
    void join_threads() {
        for (std::thread& t : listeners) {
            if (t.joinable()) {
                t.join();
            }
        }
        listeners.clear();
        {
            const std::lock_guard<std::mutex> lock(watch_mutex);
            watch_stop = true;
        }
        watch_cv.notify_all();
        if (watchdog.joinable()) {
            watchdog.join();
        }
    }

    UploadServerOptions options;
    std::vector<std::string> names_storage;
    std::vector<std::string_view> names;
    std::mutex random_mutex;
    std::random_device random;
    ConnectionLimiter limiter;
    std::atomic<bool> closing{false}; // the run is ending: every connection is closed
    UploadApi api;
    std::mutex servers_mutex;
    std::vector<std::unique_ptr<httplib::Server>> servers;
    std::vector<std::thread> listeners;
    std::thread watchdog;
    std::mutex watch_mutex;
    std::condition_variable watch_cv;
    bool watch_stop = false;
    std::atomic<int> bound_port{0};
    std::vector<std::string> bound_addresses;
    mutable std::mutex state_mutex;
};

UploadServer::UploadServer(UploadServerOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {
    impl_->names_storage = impl_->options.host_names;
    impl_->names.assign(impl_->names_storage.begin(), impl_->names_storage.end());
}

UploadServer::~UploadServer() { stop(); }

bool UploadServer::start(std::string* error) {
    Impl& s = *impl_;
    if (s.any_running() && s.api.status() != UploadStatus::Off) {
        return true;
    }
    s.stop_servers();
    s.join_threads(); // a run that turned itself off
    s.closing = false;
    s.api.start();
    const std::vector<std::string> wanted = planned_addresses(s.options);
    int port = s.options.port;
    std::vector<std::unique_ptr<httplib::Server>> made;
    std::vector<std::string> bound;
    for (const std::string& address : wanted) {
        auto server = std::make_unique<httplib::Server>();
        s.configure(*server);
        bool ok = false;
        if (port == 0) {
            const int any = server->bind_to_any_port(address);
            ok = any > 0;
            port = ok ? any : port; // the others listen on the same port
        } else {
            ok = server->bind_to_port(address, port);
        }
        if (ok) {
            made.push_back(std::move(server));
            bound.push_back(address);
        }
    }
    if (made.empty()) {
        const std::string why = "The port " + std::to_string(s.options.port) + " is taken";
        s.api.fail(why);
        if (error != nullptr) {
            *error = why;
        }
        return false;
    }
    {
        const std::lock_guard<std::mutex> lock(s.servers_mutex);
        s.servers = std::move(made);
    }
    {
        const std::lock_guard<std::mutex> lock(s.state_mutex);
        s.bound_addresses = std::move(bound);
    }
    s.bound_port = port;
    s.limiter.listen_on({port});
    s.watch_stop = false;
    for (const auto& server : s.servers) {
        s.listeners.push_back(spawn_thread([&server] { server->listen_after_bind(); }));
    }
    s.watchdog = spawn_thread([&s] { s.watch(); });
    // Ready when every listener runs; bounded, so one that failed cannot hold the caller.
    for (int i = 0; i < 2000; ++i) {
        bool all = true;
        for (const auto& server : s.servers) {
            all = all && server->is_running();
        }
        if (all) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

void UploadServer::stop() {
    Impl& s = *impl_;
    s.end_connections();
    s.join_threads();
    s.bound_port = 0;
    s.limiter.listen_on({});
    {
        const std::lock_guard<std::mutex> lock(s.state_mutex);
        s.bound_addresses.clear();
    }
    s.api.stop();
}

UploadStatus UploadServer::status() const { return impl_->api.status(); }

std::string UploadServer::pin() const {
    return status() == UploadStatus::Off || status() == UploadStatus::Error ? std::string()
                                                                            : impl_->api.pin();
}

int UploadServer::port() const {
    return status() == UploadStatus::Off ? 0 : impl_->bound_port.load();
}

std::vector<std::string> UploadServer::addresses() const {
    if (status() == UploadStatus::Off) {
        return {};
    }
    const std::lock_guard<std::mutex> lock(impl_->state_mutex);
    return impl_->bound_addresses;
}

std::string UploadServer::reason() const { return impl_->api.reason(); }

UploadMs UploadServer::remaining_ms() const { return impl_->api.remaining_ms(); }

UploadProgress UploadServer::progress() const { return impl_->api.progress(); }

std::vector<std::string> planned_addresses(const UploadServerOptions& options) {
    return !options.bind.empty()
               ? options.bind
               : choose_listen_addresses(local_ipv4_addresses(), options.all_interfaces);
}

std::vector<std::string> local_ipv4_addresses() {
    std::vector<std::string> out;
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) {
        return out;
    }
    for (const ifaddrs* a = list; a != nullptr; a = a->ifa_next) {
        if (a->ifa_addr == nullptr || a->ifa_addr->sa_family != AF_INET ||
            (a->ifa_flags & IFF_UP) == 0 || (a->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        char text[INET_ADDRSTRLEN];
        const auto* in = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
        // Only addresses the server answers: a VPN's (100.64/10, say) is not on the local network.
        if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof text) != nullptr &&
            is_local_address({reinterpret_cast<const std::uint8_t*>(&in->sin_addr), 4})) {
            out.emplace_back(text);
        }
    }
    freeifaddrs(list);
    return out;
}

} // namespace etude::app
