package com.joyvault;

import java.nio.charset.StandardCharsets;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.Reader;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** Sync authentication ONLY. Edit the project-root vault-sync-key.properties and rebuild.
 * Storage identity in DeploymentKeys is separate and must remain stable. */
final class BuiltinSyncKey {
    private static final String PROOF = proof(loadPackagedKey());
    private BuiltinSyncKey() {}
    private static byte[] loadPackagedKey() {
        InputStream input=BuiltinSyncKey.class.getResourceAsStream("/vault-sync-key.properties");
        if(input==null)throw new IllegalStateException("Packaged sync configuration is missing; rebuild the server");
        try(Reader reader=new InputStreamReader(input,StandardCharsets.UTF_8)) {
            return parseKey(reader);
        } catch(IOException e) { throw new IllegalStateException("Cannot read packaged sync configuration",e); }
    }
    static byte[] parseKey(Reader input) throws IOException {
        BufferedReader reader=new BufferedReader(input);
        String line,key=null;boolean first=true;
        while((line=reader.readLine())!=null) {
            if(first && line.startsWith("\uFEFF"))line=line.substring(1);
            first=false;line=line.trim();
            if(line.isEmpty() || line.startsWith("#"))continue;
            if(key!=null || !line.matches("sync\\.key\\.hex[ \\t]*=[ \\t]*[0-9a-fA-F]{64}"))
                throw new IllegalArgumentException("Use one sync.key.hex entry with 64 hexadecimal characters");
            key=line.substring(line.indexOf('=')+1).trim();
        }
        if(key==null)throw new IllegalArgumentException("Missing sync.key.hex");
        return Java8.unhex(key);
    }
    static String proof() { return PROOF; }
    static String proof(byte[] key) {
        if(key==null || key.length!=32)throw new IllegalArgumentException("Invalid sync key");
        try {
            MessageDigest digest=MessageDigest.getInstance("SHA-256");
            digest.update("JYVAULT-DEPLOY-KEY-V2".getBytes(StandardCharsets.US_ASCII));
            return Java8.hex(digest.digest(key));
        } catch(NoSuchAlgorithmException e) { throw new IllegalStateException("SHA-256 unavailable"); }
    }
}
