// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.loader;

import java.lang.reflect.Method;

/**
 * CloudStream's "DNS over HTTPS" setting (dns_key, SettingsGeneral) for the clients plugins use: as
 * CloudStream's initClient does when the setting changes, `app` (and `insecureApp`) get a client whose
 * resolver is the chosen DoH provider, built by CloudStream's own DohProviders.kt. Useful where an ISP
 * blocks a provider's site by DNS. The value is CloudStream's (dns_pref_values): 0 none (the system
 * resolver), 1 Google, 2 Cloudflare, 4 AdGuard, 5 DNS.WATCH, 6 Quad9, 7 DNS.SB, 8 Canadian Shield.
 *
 * OkHttp and nicehttp are on the runtime's class path (cloudstream.jar) but not on its compile class path,
 * so they are reached by reflection. Each client keeps its other settings (newBuilder); only the resolver
 * changes. Plugins that copied a client earlier keep its resolver, as on Android.
 */
final class Network {
    private Network() {}

    /** The DohProviders.kt function for a dns_key value; null for 0 (system resolver). */
    static String provider(int dns) {
        switch (dns) {
            case 0: return null;
            case 1: return "addGoogleDns";
            case 2: return "addCloudFlareDns";
            case 4: return "addAdGuardDns";
            case 5: return "addDNSWatchDns";
            case 6: return "addQuad9Dns";
            case 7: return "addDnsSbDns";
            case 8: return "addCanadianShieldDns";
            default: throw new IllegalArgumentException("unknown dns value " + dns + " (CloudStream's dns_pref_values)");
        }
    }

    /** Applies a dns_key value to app and insecureApp; returns how many clients changed. */
    static int apply(int dns) throws Exception {
        String add = provider(dns);
        Class<?> holder = Class.forName("com.lagradost.cloudstream3.MainActivityKt");
        Class<?> clientType = Class.forName("okhttp3.OkHttpClient");
        Class<?> builderType = Class.forName("okhttp3.OkHttpClient$Builder");
        Method doh = add == null ? null
                : Class.forName("com.lagradost.cloudstream3.network.DohProvidersKt").getMethod(add, builderType);
        Class<?> dnsType = Class.forName("okhttp3.Dns");
        int changed = 0;
        for (String getter : new String[]{"getApp", "getInsecureApp"}) {
            Object requests = holder.getMethod(getter).invoke(null);
            if (requests == null) continue;
            Object client = requests.getClass().getMethod("getBaseClient").invoke(requests);
            Object builder = clientType.getMethod("newBuilder").invoke(client);
            if (doh == null) {
                builderType.getMethod("dns", dnsType).invoke(builder, dnsType.getField("SYSTEM").get(null));
            } else {
                doh.invoke(null, builder);
            }
            Object built = builderType.getMethod("build").invoke(builder);
            requests.getClass().getMethod("setBaseClient", clientType).invoke(requests, built);
            changed++;
        }
        return changed;
    }
}
