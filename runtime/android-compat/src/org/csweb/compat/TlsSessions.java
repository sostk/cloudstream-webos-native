// SPDX-License-Identifier: GPL-3.0-or-later
package org.csweb.compat;

import java.security.KeyManagementException;
import java.security.NoSuchAlgorithmException;
import java.security.Provider;
import java.security.SecureRandom;
import java.security.Security;
import java.util.ArrayList;
import java.util.List;

import javax.net.ssl.KeyManager;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLContextSpi;
import javax.net.ssl.SSLEngine;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLServerSocketFactory;
import javax.net.ssl.SSLSessionContext;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;

/**
 * TLS without client session resumption, for every SSLContext the runtime creates (docs/runtime/tls.md).
 *
 * The runtime's JSSE provider, wolfJSSE, mixes up concurrent handshakes that share its client session
 * cache: with several connections to one host at once (OkHttp's fast fallback, plugins' parallel
 * requests), two in three sessions came back with no peer certificates, so OkHttp refused them
 * ("Hostname ... not verified (no certificates)"), and some failed with "Object has been freed". With
 * the cache size set to 0 every handshake is a full one, and 72 of 72 concurrent handshakes succeeded.
 * wolfJSSE has no setting for this, and OkHttp makes its own SSLContext for each client, so a provider
 * placed first gives out the same provider's contexts with the cache turned off. Everything else
 * (protocols, cipher suites, certificate and host name checks) is the wrapped provider's own.
 */
public final class TlsSessions {
    public static final String PROVIDER = "CswebTlsNoResume";

    private TlsSessions() {}

    /**
     * Places the wrapper first. Wraps the provider that currently serves SSLContext "TLS"; returns its
     * name, or null if there is none. Safe to call more than once.
     */
    public static synchronized String install() {
        Provider existing = Security.getProvider(PROVIDER);
        if (existing != null) return ((Wrapper) existing).base.getName();
        Provider base = null;
        for (Provider p : Security.getProviders()) {
            if (p.getService("SSLContext", "TLS") != null) {
                base = p;
                break;
            }
        }
        if (base == null) return null;
        Security.insertProviderAt(new Wrapper(base), 1);
        return base.getName();
    }

    static void noCache(SSLContext c) {
        SSLSessionContext s = c.getClientSessionContext();
        if (s != null) s.setSessionCacheSize(0);
    }

    static final class Wrapper extends Provider {
        private static final long serialVersionUID = 1L;
        final transient Provider base;

        Wrapper(Provider base) {
            super(PROVIDER, 1.0, "SSLContext of " + base.getName() + " without client session resumption");
            this.base = base;
            List<Provider.Service> wrapped = new ArrayList<>();
            for (Provider.Service s : base.getServices()) {
                if (s.getType().equals("SSLContext")) wrapped.add(new ContextService(this, s.getAlgorithm()));
            }
            for (Provider.Service s : wrapped) putService(s);
        }
    }

    static final class ContextService extends Provider.Service {
        ContextService(Wrapper p, String algorithm) {
            super(p, "SSLContext", algorithm, NoResumeContext.class.getName(), null, null);
        }

        @Override
        public Object newInstance(Object constructorParameter) throws NoSuchAlgorithmException {
            Provider base = ((Wrapper) getProvider()).base;
            return new NoResumeContext(SSLContext.getInstance(getAlgorithm(), base));
        }
    }

    static final class NoResumeContext extends SSLContextSpi {
        private final SSLContext d;

        NoResumeContext(SSLContext d) {
            this.d = d;
            try {
                noCache(d);  // "Default" comes initialised
            } catch (RuntimeException notInitialised) {
                // set in engineInit
            }
        }

        @Override
        protected void engineInit(KeyManager[] km, TrustManager[] tm, SecureRandom sr) throws KeyManagementException {
            d.init(km, tm, sr);
            noCache(d);
        }

        @Override
        protected SSLSocketFactory engineGetSocketFactory() {
            return d.getSocketFactory();
        }

        @Override
        protected SSLServerSocketFactory engineGetServerSocketFactory() {
            return d.getServerSocketFactory();
        }

        @Override
        protected SSLEngine engineCreateSSLEngine() {
            return d.createSSLEngine();
        }

        @Override
        protected SSLEngine engineCreateSSLEngine(String host, int port) {
            return d.createSSLEngine(host, port);
        }

        @Override
        protected SSLSessionContext engineGetServerSessionContext() {
            return d.getServerSessionContext();
        }

        @Override
        protected SSLSessionContext engineGetClientSessionContext() {
            return d.getClientSessionContext();
        }

        @Override
        protected SSLParameters engineGetDefaultSSLParameters() {
            return d.getDefaultSSLParameters();
        }

        @Override
        protected SSLParameters engineGetSupportedSSLParameters() {
            return d.getSupportedSSLParameters();
        }
    }
}
