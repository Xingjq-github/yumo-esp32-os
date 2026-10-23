package com.joyvault;

import java.util.*;

final class Entries {
    static final int MAX_NOTE_BYTES=256;
    static String limitNote(String value) {
        byte[] bytes=Json.utf8(value);
        if(bytes.length<=MAX_NOTE_BYTES)return value;
        int end=MAX_NOTE_BYTES;
        while(end>0&&(bytes[end]&0xc0)==0x80)end--;
        return new String(bytes,0,end,java.nio.charset.StandardCharsets.UTF_8);
    }
    static boolean printable(String value){return value.codePoints().allMatch(c->c>=32&&c<=126);}
    static final Comparator<Map<String,Object>> ACCOUNT_ORDER=Comparator
        .comparing((Map<String,Object> entry)->(String)entry.get("username"),String.CASE_INSENSITIVE_ORDER)
        .thenComparing(entry->(String)entry.get("username"));
    static boolean sortWebByAccount(List<Map<String,Object>> entries) {
        List<Map<String,Object>> sorted=new ArrayList<>();
        for(Map<String,Object> entry:entries)if("web".equals(entry.get("category")))sorted.add(entry);
        sorted.sort(ACCOUNT_ORDER);boolean changed=false;int next=0;
        // Preserve other categories and every credential; reorder web slots only.
        for(int i=0;i<entries.size();i++)if("web".equals(entries.get(i).get("category"))) {
            Map<String,Object> entry=sorted.get(next++);
            changed|=!entry.equals(entries.get(i));entries.set(i,entry);
        }
        return changed;
    }
    static final Map<String,String> CATEGORIES;
    static { Map<String,String> c=new LinkedHashMap<>();c.put("work","工作");c.put("app","应用");c.put("web","网页");c.put("other","其他");CATEGORIES=Collections.unmodifiableMap(c); }
    static final Set<String> FIELDS=Java8.set("id","title","username","password","target","note","category","favorite");
    static Map<String,Object> validate(Map<String,Object> body,Map<String,Object> old) {
        Map<String,Object> result=new LinkedHashMap<>();
        boolean code="other".equals(body.get("category"));
        String[] names={"title","username","password","target","note"};int[] limits={96,128,128,192,MAX_NOTE_BYTES};
        for(int i=0;i<names.length;i++) {
            String name=names[i]; Object value=body.getOrDefault(name,"");
            // The field remains in the wire format for older devices/archives.
            // Hide legacy accounts without erasing them when an existing code is edited.
            if(code && name.equals("username"))value=old==null?"":old.get("username");
            if(!(value instanceof String))throw ApiException.bad("字段格式或长度不正确");
            String text=(String)value;
            if(name.equals("note")) {
                if(text.codePoints().anyMatch(c->c<32||c==127))throw ApiException.bad("字段格式或长度不正确");
                text=limitNote(text);
            }
            if(Json.utf8(text).length>limits[i] || text.codePoints().anyMatch(c->c<32 || c==127))throw ApiException.bad("字段格式或长度不正确");
            if((name.equals("username") || name.equals("password")) && text.codePoints().anyMatch(c->c>126))throw ApiException.bad(code?"码值仅支持英文、数字和符号":"账号和密码仅支持英文、数字和符号");
            result.put(name,text);
        }
        if(result.get("password").equals("") && old!=null)result.put("password",old.get("password"));
        if(Json.strip((String)result.get("title")).isEmpty() || result.get("password").equals(""))throw ApiException.bad(code?"名称和码值不能为空":"名称和密码不能为空");
        if(!(body.get("category") instanceof String) || !CATEGORIES.containsKey(body.get("category")))throw ApiException.bad("请选择分类");
        result.put("category",body.get("category"));result.put("id",old==null?VaultProtocol.randomHex(16):old.get("id"));result.put("favorite",false);return result;
    }
    static Map<String,Object> metadata(Map<String,Object> entry) {
        Map<String,Object> m=new LinkedHashMap<>(entry);
        if("other".equals(entry.get("category")))m.remove("username");
        else m.remove("password");
        return m;
    }
    static Map<String,Object> visible(Map<String,Object> entry,boolean plaintext) {
        Map<String,Object> m=metadata(entry);
        if(plaintext)m.put("password",entry.get("password"));
        return m;
    }
}
