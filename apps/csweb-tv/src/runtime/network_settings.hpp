// SPDX-License-Identifier: GPL-3.0-or-later
//
// CloudStream's "DNS over HTTPS" setting for the plugin runtime (the "network" request,
// docs/runtime/service-protocol.md): sent when the runtime starts, before any plugin loads, and again
// whenever the setting changes, as CloudStream calls initClient. Plugins' HTTP requests then resolve
// through the chosen provider, which gets past DNS blocks by an ISP. The app's own requests (images,
// playback) use the kit's HTTP client and the system resolver.
#pragma once

#include "runtime/runtime_client.hpp"

#include <nlohmann/json.hpp>

namespace csweb::runtime {

nlohmann::json dns_request(int dns_value);
// Sends dns_request(dns_value); logs the outcome.
void send_dns(RuntimeClient &rt, int dns_value);

}  // namespace csweb::runtime
