// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime/network_settings.hpp"

#include "wnk/log.hpp"

namespace csweb::runtime {

namespace {
constexpr const char *kCat = "csweb";
}

nlohmann::json dns_request(int dns_value) { return {{"op", "network"}, {"dns", dns_value}}; }

void send_dns(RuntimeClient &rt, int dns_value) {
    rt.request(dns_request(dns_value), nullptr, [dns_value](bool ok, const std::string &error) {
        if (ok) WNK_LOGI(kCat, "runtime: DNS over HTTPS %d applied", dns_value);
        else WNK_LOGW(kCat, "runtime: DNS over HTTPS %d not applied: %s", dns_value, error.c_str());
    });
}

}  // namespace csweb::runtime
