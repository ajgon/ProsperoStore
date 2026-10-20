// ProsperoStore - The store's own update: where its archive may be downloaded from.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
// The console's update path (src/platform/ps5/self_update_store.cpp) with the kit, the
// transport's address rules and the catalog's as they ship; only the network, the helper and
// the clock are stand-ins.
#include "platform/ps5/system.hpp"
#include "system/self_update_store.hpp"
#include "system/worker_launch.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace store;
// Every address the transport really asked for. Written by the kit's thread, read after
// update_self() has joined it.
std::vector<std::string> requested;

namespace store::net
{
Response request_once(const std::string &url, std::uint64_t, const Sink &, Control &,
                      const std::string &)
{
    requested.push_back(url);
    Response response;
    response.error = "The test has no network";
    return response;
}
} // namespace store::net

namespace store::system
{
// The helper as the kit sees it: it agrees to the update and takes whatever it is sent.
bool launch_program(const char *, install::Channel &channel)
{
    auto answer = std::make_shared<std::string>("ready\n");
    channel.send = [](const void *, std::size_t size) { return static_cast<long>(size); };
    channel.receive = [answer](void *data, std::size_t size) -> long
    {
        if (answer->empty() || size == 0)
            return 0;
        *static_cast<char *>(data) = answer->front();
        answer->erase(0, 1);
        return 1;
    };
    channel.close = [] {};
    return true;
}
} // namespace store::system

namespace hui::sys
{
std::int64_t monotonic_us()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
void sleep_us(std::uint32_t microseconds)
{
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}
} // namespace hui::sys

// One update of the store to the archive at artifact, with catalog as the custom catalog the
// user set: the addresses asked for. It never completes, since the test has no network.
static std::vector<std::string> attempt(const std::string &artifact, const std::string &catalog)
{
    requested.clear();
    catalog::Entry entry;
    entry.id = "PPSA99000";
    entry.name = "ProsperoStore";
    entry.version = "1.000.104";
    entry.content_version = "01.000.104";
    entry.artifact = artifact;
    entry.digest = std::string(64, '0');
    entry.size = 1024;
    install::Progress progress;
    net::Control control;
    std::string error;
    assert(!system::update_self(entry, "01.000.103", catalog, progress, control, error));
    assert(error == "The download failed");
    return requested;
}

int main()
{
    using Asked = std::vector<std::string>;
    const std::string own = "https://store.example/api/v1/";
    const std::string file = "https://store.example/files/PPSA99000.zip";
    const std::string github =
        "https://github.com/owner/ProsperoStore/releases/download/v1.000.104/PPSA99000.zip";
    // The custom catalog's own host serves the store's archive, as it does any app's.
    assert(attempt(file, own) == Asked{file});
    assert(attempt("https://STORE.example/files/PPSA99000.zip", own) ==
           Asked{"https://STORE.example/files/PPSA99000.zip"});
    // GitHub stays allowed, with a custom catalog or without one.
    assert(attempt(github, own) == Asked{github});
    assert(attempt(github, "") == Asked{github});
    assert(attempt(github, std::string(catalog::kDefaultApi)) == Asked{github});
    // No custom catalog in use (none, or the official address): its host is not asked.
    assert(attempt(file, "").empty());
    assert(attempt(file, std::string(catalog::kDefaultApi)).empty());
    // Only the custom catalog's own host and port, and never the official catalog's places.
    for (const char *other :
         {"https://cdn.store.example/PPSA99000.zip", "https://store.example:8443/PPSA99000.zip",
          "https://store.example.evil/PPSA99000.zip", "http://store.example/PPSA99000.zip"})
        assert(attempt(other, own).empty());
    assert(attempt("https://prosperostore.net/PPSA99000.zip", std::string(catalog::kDefaultApi))
               .empty());
    std::puts("Store self-update download address checks passed.");
}
