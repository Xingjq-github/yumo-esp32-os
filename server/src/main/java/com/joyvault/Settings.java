package com.joyvault;

import java.net.URI;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Locale;
import org.springframework.core.env.Environment;

public final class Settings {
    private final Path data,keys;
    private final String origin;
    private final int port;
    public Settings(Path data,Path keys,String origin,int port){this.data=data;this.keys=keys;this.origin=origin;this.port=port;}
    public Path data(){return data;}
    public Path keys(){return keys;}
    public String origin(){return origin;}
    public int port(){return port;}
    static String normalize(String value) {
        try {
            if (value == null || value.length()>240 || value.contains("\\") || value.contains("\r") || value.contains("\n")) throw new Exception();
            URI u = new URI(value);
            String scheme = u.getScheme() == null ? "" : u.getScheme().toLowerCase(Locale.ROOT);
            String host = u.getHost();
            if (!(scheme.equals("http") || scheme.equals("https")) || host == null ||
                !host.matches("[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?") || u.getRawUserInfo()!=null ||
                u.getRawQuery()!=null || u.getRawFragment()!=null ||
                !(u.getRawPath()==null || u.getRawPath().isEmpty() || u.getRawPath().equals("/"))) throw new Exception();
            int port=u.getPort()==-1?(scheme.equals("https")?443:80):u.getPort();
            if (port<1 || port>65535) throw new Exception();
            return scheme+"://"+host.toLowerCase(Locale.ROOT)+(port==(scheme.equals("https")?443:80)?"":":"+port);
        } catch (Exception e) { throw new IllegalArgumentException("VAULT_PUBLIC_URL must be an HTTP(S) origin with a valid port"); }
    }
    static Settings from(Environment env) {
        String origin=normalize(env.getProperty("vault.public-url",env.getProperty("VAULT_PUBLIC_URL","http://127.0.0.1:8443")));
        boolean tls=env.getProperty("server.ssl.enabled",Boolean.class,false);
        if (origin.startsWith("https:") != tls) {
            throw new IllegalArgumentException("VAULT_PUBLIC_URL scheme must match server.ssl.enabled; HTTP is the default");
        }
        int port=URI.create(origin).getPort();
        return new Settings(Paths.get(env.getProperty("VAULT_DATA_DIR","java-native/data")).toAbsolutePath().normalize(),
            Paths.get(env.getProperty("VAULT_KEYS_FILE","deployment/local-keys.json")).toAbsolutePath().normalize(),origin,
            port==-1?(origin.startsWith("https:")?443:80):port);
    }
}
