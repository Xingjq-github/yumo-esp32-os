package com.joyvault;

import java.nio.file.*;
import java.util.*;

/** External fixed deployment identity; never auto-generate or include in logs. */
public final class DeploymentKeys {
    final byte[] key, id, salt;
    final String token;
    DeploymentKeys(byte[] key, String token, byte[] id, byte[] salt) {
        if(key.length!=32 || id.length!=16 || salt.length!=16 || !token.matches("[0-9a-fA-F]{64}"))
            throw new IllegalArgumentException("Invalid deployment key format");
        this.key=key.clone();
        this.token=token; this.id=id.clone(); this.salt=salt.clone();
    }
    static DeploymentKeys load(Path path) {
        byte[] data=null;
        try {
            if(Files.size(path)>4096) throw new IllegalArgumentException();
            data=Files.readAllBytes(path);
            Map<String,Object> value=Json.object(data);
            if(!Json.integerEquals(value.get("schema"),2)) throw new IllegalArgumentException();
            return new DeploymentKeys(hex(value,"key",32),(String)value.get("token"),hex(value,"vault_id",16),hex(value,"salt",16));
        } catch(Exception e) { throw new IllegalStateException("Cannot read existing deployment keys; no keys were generated or changed"); }
        finally { if(data!=null) Arrays.fill(data,(byte)0); }
    }
    static byte[] hex(Map<String,Object> map,String field,int size) {
        Object v=map.get(field);
        if(!(v instanceof String) || !((String)v).matches("[0-9a-fA-F]{"+(size*2)+"}")) throw new IllegalArgumentException();
        return Java8.unhex((String)v);
    }
    @Override public String toString() { return "DeploymentKeys[redacted]"; }
}
