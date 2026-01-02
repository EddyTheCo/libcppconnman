#include <gtest/gtest.h>

#include <amarula/dbus/gdbus.hpp>
#include <amarula/dbus/gproxy.hpp>
#include <memory>
#include <stdexcept>

using Amarula::DBus::G::DBus;
using Amarula::DBus::G::DBusProxy;

TEST(DBus, Initialization) {
    EXPECT_NO_THROW(
        { const DBus dbus("org.freedesktop.DBus", "/org/freedesktop/DBus"); });
    EXPECT_THROW(
        { const DBus dbus("invalid.bus.name", "/invalid/object/path"); },
        std::runtime_error);
}

struct FooProperties {
   public:
    static void print() { std::cerr << "Foo Foo Foo\n"; };

    void update(const gchar* key, GVariant* value) {
        std::cerr << "Key: " << key << '\n';
    }

    friend class DBusProxy<FooProperties>;
};

class FooProxy : public DBusProxy<FooProperties> {
   public:
    explicit FooProxy(DBus* dbus)
        : DBusProxy(dbus, "net.connman", "/", "net.connman.Clock") {}
};

TEST(DBus, PropertiesCallbackRunsInWorkerThread) {
    std::atomic<bool> pump_running{true};
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);

    std::thread::id main_tid = std::this_thread::get_id();
    std::thread::id callback_tid;
    std::thread::id loop_tid;

    std::thread qt_simulator([&] {
        loop_tid = std::this_thread::get_id();
        g_main_loop_run(loop);
        g_main_loop_unref(loop);
    });

    std::mutex mtx;
    std::condition_variable cond_v;
    bool callback_called = false;

    const auto dbus =
        std::make_unique<DBus>("org.freedesktop.DBus", "/org/freedesktop/DBus");
    auto proxy = std::make_shared<FooProxy>(dbus.get());

    proxy->getProperties([&](const FooProperties&) {
        callback_tid = std::this_thread::get_id();
        {
            std::lock_guard<std::mutex> lock(mtx);
            callback_called = true;
        }
        cond_v.notify_one();
    });

    {
        std::unique_lock<std::mutex> lock(mtx);
        ASSERT_TRUE(cond_v.wait_for(lock, std::chrono::seconds(2),
                                    [&] { return callback_called; }));
    }

    g_main_loop_quit(loop);
    qt_simulator.join();

    std::cerr << "callback_tid: " << callback_tid << '\n';
    std::cerr << "main_tid: " << main_tid << '\n';
    std::cerr << "loop_tid: " << loop_tid << '\n';
    EXPECT_NE(callback_tid, main_tid);
    EXPECT_NE(callback_tid, loop_tid);
}
