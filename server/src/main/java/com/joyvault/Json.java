package com.joyvault;

import com.fasterxml.jackson.core.StreamReadConstraints;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.*;
import java.util.*;

final class Json {
    static final ObjectMapper MAPPER = new ObjectMapper()
        .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS);
    static { MAPPER.getFactory().setStreamReadConstraints(StreamReadConstraints.builder()
        .maxNestingDepth(64).maxStringLength(1048576).maxNumberLength(32).build()); }
    private Json() {}
    static byte[] bytes(Object value) {
        try { return MAPPER.writeValueAsBytes(value); }
        catch (Exception e) { throw new IllegalStateException("JSON serialization failed"); }
    }
    static Map<String,Object> object(byte[] input) {
        try {
            Map<String,Object> map = MAPPER.readValue(input, new TypeReference<LinkedHashMap<String,Object>>() {});
            if (map == null) throw ApiException.bad("请求格式错误");
            return map;
        } catch (Exception e) { throw ApiException.bad("请求格式错误"); }
    }
    static byte[] utf8(String value) {
        try {
            ByteBuffer bytes = StandardCharsets.UTF_8.newEncoder().onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT).encode(CharBuffer.wrap(value));
            byte[] result = new byte[bytes.remaining()]; bytes.get(result); return result;
        } catch (CharacterCodingException e) { throw ApiException.bad("字段格式或长度不正确"); }
    }
    static Map<String,Object> map(Object... pairs) {
        Map<String,Object> value = new LinkedHashMap<>();
        for (int i=0;i<pairs.length;i+=2) value.put((String)pairs[i],pairs[i+1]);
        return value;
    }
    static boolean integerEquals(Object value,long expected) {
        return (value instanceof Integer || value instanceof Long || value instanceof java.math.BigInteger)
            && value.toString().equals(Long.toString(expected));
    }
    // Python's original strip() includes NBSP and the NEL character; Java strip()
    // does not. Apply only to validation/comparison, never mutate credentials.
    static String strip(String value) {
        int start=0,end=value.length();
        while(start<end && whitespace(value.charAt(start)))start++;
        while(end>start && whitespace(value.charAt(end-1)))end--;
        return value.substring(start,end);
    }
    private static boolean whitespace(char c) {
        return (c>=9 && c<=13) || (c>=0x1c && c<=0x20) || c==0x85 || c==0xa0 ||
            c==0x1680 || (c>=0x2000 && c<=0x200a) || c==0x2028 || c==0x2029 ||
            c==0x202f || c==0x205f || c==0x3000;
    }
    static int characters(String s) { return s.codePointCount(0,s.length()); }
}
