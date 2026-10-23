package com.joyvault;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.*;

/** Small Java 8 replacements; no change to wire encoding or collection ownership. */
final class Java8 {
    private static final char[] HEX="0123456789abcdef".toCharArray();
    private Java8() {}
    static String hex(byte[] bytes) { return hex(bytes,0,bytes.length); }
    static String hex(byte[] bytes,int start,int end) {
        if(start<0 || end<start || end>bytes.length)throw new IndexOutOfBoundsException();
        char[] out=new char[(end-start)*2];
        for(int i=start,j=0;i<end;i++){int b=bytes[i]&255;out[j++]=HEX[b>>>4];out[j++]=HEX[b&15];}
        return new String(out);
    }
    static byte[] unhex(String text) {
        if((text.length()&1)!=0)throw new IllegalArgumentException("Invalid hex");
        byte[] out=new byte[text.length()/2];
        for(int i=0;i<out.length;i++){
            int hi=digit(text.charAt(i*2)),lo=digit(text.charAt(i*2+1));
            if(hi<0 || lo<0)throw new IllegalArgumentException("Invalid hex");
            out[i]=(byte)((hi<<4)|lo);
        }
        return out;
    }
    private static int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
    @SafeVarargs static <T> List<T> list(T... values) {
        List<T> copy=new ArrayList<>(values.length);
        for(T value:values)copy.add(Objects.requireNonNull(value));
        return Collections.unmodifiableList(copy);
    }
    @SafeVarargs static <T> Set<T> set(T... values) {
        Set<T> copy=new LinkedHashSet<>();
        for(T value:values)if(!copy.add(Objects.requireNonNull(value)))throw new IllegalArgumentException("Duplicate set element");
        return Collections.unmodifiableSet(copy);
    }
    @SuppressWarnings("unchecked") static <K,V> Map<K,V> map(Object... pairs) {
        if((pairs.length&1)!=0)throw new IllegalArgumentException("Expected key/value pairs");
        Map<K,V> result=new LinkedHashMap<>();
        for(int i=0;i<pairs.length;i+=2){K key=(K)Objects.requireNonNull(pairs[i]);V value=(V)Objects.requireNonNull(pairs[i+1]);if(result.put(key,value)!=null)throw new IllegalArgumentException("Duplicate map key");}
        return Collections.unmodifiableMap(result);
    }
    static byte[] read(InputStream input,int maximum)throws IOException {
        ByteArrayOutputStream out=new ByteArrayOutputStream();byte[] buffer=new byte[4096];
        for(int n;(n=input.read(buffer))!=-1;){if(n>maximum-out.size())throw new IOException("Input exceeds limit");out.write(buffer,0,n);}
        return out.toByteArray();
    }
}
