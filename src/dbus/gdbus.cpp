#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <glibconfig.h>

#include <amarula/dbus/gdbus.hpp>
#include <mutex>
#include <stdexcept>
#include <string>

namespace Amarula::DBus::G {

void DBus::onAnyAsyncDone() {
    std::lock_guard<std::mutex> const lock(mtx_);
    if (pending_calls_-- == 1 && !running_ && loop_ != nullptr) {
        g_main_loop_quit(loop_);
    }
}

void DBus::onAnyAsyncStart() { ++pending_calls_; }

DBus::DBus(const std::string& bus_name, const std::string& object_path)
    : ctx_{g_main_context_new()} {
    glib_thread_ = std::thread([this]() {
        GError* error = nullptr;
        loop_ = g_main_loop_new(ctx_, FALSE);
        g_main_context_invoke(ctx_, &DBus::on_loop_started, this);
        g_main_loop_run(loop_);
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    });
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return running_; });
    }
}

DBus::~DBus() {
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return running_; });
    }
    {
        std::lock_guard<std::mutex> const lock(mtx_);
        running_ = false;
    }
    if (loop_ != nullptr && (g_main_loop_is_running(loop_) == TRUE) &&
        pending_calls_ == 0U) {
        g_main_loop_quit(loop_);
    }
    if (glib_thread_.joinable()) {
        glib_thread_.join();
    }
    if (connection_ != nullptr) {
        g_object_unref(connection_);
    }
    g_main_context_unref(ctx_);
}

auto DBus::on_loop_started(gpointer user_data) -> gboolean {
    auto* self = static_cast<DBus*>(user_data);
    {
        std::lock_guard<std::mutex> const lock(self->mtx_);
        self->running_ = true;
    }
    self->cv_.notify_all();
    return G_SOURCE_REMOVE;  // run once
}

}  // namespace Amarula::DBus::G
