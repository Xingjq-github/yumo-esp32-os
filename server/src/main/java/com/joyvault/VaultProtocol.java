package com.joyvault;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import java.nio.*;
import java.security.*;
import java.util.*;

/** Byte-for-byte JYVAULT2 envelope: little-endian header, AES-GCM, unchanged AADs. */
public final class VaultProtocol {
    public static final int MAX_BUNDLE=262144, MAX_ENTRIES=256;
    private static final byte[] MAGIC="JYVAULT2".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final SecureRandom RANDOM=new SecureRandom();
    final DeploymentKeys keys;
    VaultProtocol(DeploymentKeys keys) { this.keys=keys; }
    static byte[] random(int length) { byte[] b=new byte[length]; RANDOM.nextBytes(b); return b; }
    static String randomHex(int length) { return Java8.hex(random(length)); }
    static boolean nonzero(byte[] b,int from,int to) { for(int i=from;i<to;i++) if(b[i]!=0)return true; return false; }
    static Map<String,Object> inspect(byte[] blob) {
        if(blob.length<144 || blob.length>MAX_BUNDLE || !Arrays.equals(MAGIC,Arrays.copyOf(blob,8))) throw new IllegalArgumentException("Invalid JYVAULT2 bundle");
        ByteBuffer b=ByteBuffer.wrap(blob).order(ByteOrder.LITTLE_ENDIAN);
        if(b.getInt(8)!=0 || Integer.toUnsignedLong(b.getInt(12))!=blob.length-128 || b.getInt(20)!=0 || !nonzero(blob,24,40) || !nonzero(blob,40,56)) throw new IllegalArgumentException("Invalid JYVAULT2 header");
        return Json.map("schema",2,"revision",Integer.toUnsignedLong(b.getInt(16)),"vault_id",Java8.hex(blob,24,40),"salt",Java8.hex(blob,40,56),"size",blob.length);
    }
    private static byte[] crypt(int mode,byte[] key,byte[] nonce,byte[] value,byte[] aad) {
        try {
            Cipher c=Cipher.getInstance("AES/GCM/NoPadding"); c.init(mode,new SecretKeySpec(key,"AES"),new GCMParameterSpec(128,nonce)); c.updateAAD(aad); return c.doFinal(value);
        } catch(GeneralSecurityException e) { throw new IllegalArgumentException("Encrypted bundle authentication failed"); }
    }
    static void requirePayloadSize(byte[] plain) {
        if(plain.length>MAX_BUNDLE-144)throw ApiException.bad("密码库总大小超过256 KiB，请减少条目或备注；本次未写入");
    }
    static void checkSize(List<Map<String,Object>> entries) {
        byte[] plain=Json.bytes(Json.map("schema",2,"entries",entries));
        try{requirePayloadSize(plain);}finally{Arrays.fill(plain,(byte)0);}
    }
    byte[] encode(List<Map<String,Object>> entries,long revision) {
        if(revision<0 || revision>0xffffffffL || entries.size()>MAX_ENTRIES) throw new IllegalArgumentException("Invalid vault size or revision");
        byte[] plain=Json.bytes(Json.map("schema",2,"entries",entries)); byte[] dk=random(32);
        try {
            requirePayloadSize(plain);
            byte[] h=new byte[128]; System.arraycopy(MAGIC,0,h,0,8);
            ByteBuffer.wrap(h).order(ByteOrder.LITTLE_ENDIAN).putInt(8,0).putInt(12,plain.length+16).putInt(16,(int)revision).putInt(20,0);
            System.arraycopy(keys.id,0,h,24,16);System.arraycopy(keys.salt,0,h,40,16);
            System.arraycopy(random(12),0,h,56,12);
            byte[] wrapped=crypt(Cipher.ENCRYPT_MODE,keys.key,Arrays.copyOfRange(h,56,68),dk,Arrays.copyOf(h,68));
            System.arraycopy(wrapped,0,h,68,48);System.arraycopy(random(12),0,h,116,12);
            byte[] cipher=crypt(Cipher.ENCRYPT_MODE,dk,Arrays.copyOfRange(h,116,128),plain,h);
            byte[] blob=Arrays.copyOf(h,128+cipher.length);System.arraycopy(cipher,0,blob,128,cipher.length);inspect(blob);return blob;
        } finally { Arrays.fill(plain,(byte)0);Arrays.fill(dk,(byte)0); }
    }
    @SuppressWarnings("unchecked") List<Map<String,Object>> decode(byte[] blob) {
        inspect(blob);
        if(!MessageDigest.isEqual(keys.id,Arrays.copyOfRange(blob,24,40)) || !MessageDigest.isEqual(keys.salt,Arrays.copyOfRange(blob,40,56))) throw new IllegalArgumentException("Deployment identity mismatch");
        byte[] dk=crypt(Cipher.DECRYPT_MODE,keys.key,Arrays.copyOfRange(blob,56,68),Arrays.copyOfRange(blob,68,116),Arrays.copyOf(blob,68));
        byte[] plain=null;
        try {
            plain=crypt(Cipher.DECRYPT_MODE,dk,Arrays.copyOfRange(blob,116,128),Arrays.copyOfRange(blob,128,blob.length),Arrays.copyOf(blob,128));
            Map<String,Object> payload=Json.object(plain);
            if(!Json.integerEquals(payload.get("schema"),2) || !(payload.get("entries") instanceof List<?>) || ((List<?>)payload.get("entries")).size()>MAX_ENTRIES) throw new IllegalArgumentException("Invalid vault payload");
            List<?> entries=(List<?>)payload.get("entries");
            for(Object entry:entries) if(!(entry instanceof Map<?,?>))throw new IllegalArgumentException("Invalid vault entry");
            return (List<Map<String,Object>>)(List<?>)entries;
        } finally { Arrays.fill(dk,(byte)0);if(plain!=null)Arrays.fill(plain,(byte)0); }
    }
    static String sha256(byte[] b) {
        try{return Java8.hex(MessageDigest.getInstance("SHA-256").digest(b));}
        catch(GeneralSecurityException e){throw new IllegalStateException("SHA-256 unavailable");}
    }
}
