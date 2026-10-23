package com.joyvault;

import java.io.*;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.stream.Collectors;

/** Pure planning: no network lookups, no upload files, no database writes. */
final class ImportService {
    static final int MAX_CSV_BYTES=131072,MAX_IMPORT_BODY=1048576,MAX_ARCHIVE_BODY=350552;
    static final class Plan {
        private final List<Map<String,Object>> entries;private final Map<String,Object> report;
        Plan(List<Map<String,Object>> entries,Map<String,Object> report){this.entries=entries;this.report=report;}
        List<Map<String,Object>> entries(){return entries;}Map<String,Object> report(){return report;}
        @SuppressWarnings("unchecked") Map<String,Object> report(boolean plaintext) {
            Map<String,Object> result=new LinkedHashMap<>(report);result.put("super_mode",plaintext);
            if(plaintext) {
                Map<Object,Object> passwords=new HashMap<>();
                for(Map<String,Object> entry:entries)passwords.put(entry.get("id"),entry.get("password"));
                List<Map<String,Object>> rows=new ArrayList<>();
                for(Map<String,Object> row:(List<Map<String,Object>>)report.get("entries")) {
                    Map<String,Object> copy=new LinkedHashMap<>(row);
                    copy.put("password",passwords.get(row.get("id")));rows.add(copy);
                }
                result.put("entries",rows);
            }
            return result;
        }
    }
    static final class Archive {
        private final List<Map<String,Object>> entries;private final long revision;
        Archive(List<Map<String,Object>> entries,long revision){this.entries=entries;this.revision=revision;}
        List<Map<String,Object>> entries(){return entries;}long revision(){return revision;}
    }
    private static final class Key {
        private final String category,username,password;
        Key(String category,String username,String password){this.category=category;this.username=username;this.password=password;}
        @Override public boolean equals(Object value){if(this==value)return true;if(!(value instanceof Key))return false;Key other=(Key)value;return Objects.equals(category,other.category)&&Objects.equals(username,other.username)&&Objects.equals(password,other.password);}
        @Override public int hashCode(){return Objects.hash(category,username,password);}
        static Key of(Map<String,Object> e){return new Key((String)e.get("category"),(String)e.get("username"),(String)e.get("password"));}
        @Override public String toString(){return "CredentialKey[redacted]";}
    }
    private static final Map<Integer,String> CASEFOLD=loadCasefold();
    static Map<Integer,String> loadCasefold() {
        Map<Integer,String> map=new HashMap<>();
        try(BufferedReader reader=new BufferedReader(new InputStreamReader(Objects.requireNonNull(ImportService.class.getResourceAsStream("/casefold.txt")),StandardCharsets.UTF_8))) {
            for(String line;(line=reader.readLine())!=null;) {String[] p=line.split("=");StringBuilder b=new StringBuilder();for(String c:p[1].split(","))b.appendCodePoint(Integer.parseInt(c,16));map.put(Integer.parseInt(p[0],16),b.toString());}
            return Collections.unmodifiableMap(map);
        }catch(Exception e){throw new IllegalStateException("Unicode casefold table unavailable");}
    }
    static String fold(String s){StringBuilder out=new StringBuilder();s.codePoints().forEach(c->out.append(CASEFOLD.getOrDefault(c,new String(Character.toChars(c)))));return out.toString();}
    static String normalizedTokens(String value){return String.join(",",Arrays.stream(value.split(",",-1)).map(s->fold(Json.strip(s))).collect(Collectors.toList()));}
    static String extendNote(String note,List<String> additions) {
        String result=Entries.limitNote(note);
        if(!result.equals(note))return result;
        String seen=note.isEmpty()?",":","+normalizedTokens(note)+",";
        for(String value:additions) {
            String token=normalizedTokens(value);
            if(token.replace(",","").isEmpty()||seen.contains(","+token+","))continue;
            String prefix=result.isEmpty()?"":result+",";
            String combined=prefix+value,limited=Entries.limitNote(combined);
            // Do not leave an added separator when no following character fits.
            if(!limited.equals(combined))return limited.length()>prefix.length()?limited:result;
            result=limited;seen+=token+",";
        }
        return result;
    }
    static String noteDomain(String host){return host.substring(0,Math.min(10,host.length()));}
    static String compactKnownDomains(String note,List<String> hosts) {
        String[] parts=note.split(",",-1);
        for(int i=0;i<parts.length;i++) {
            String token=fold(Json.strip(parts[i]));
            for(String host:hosts)if(token.equals(fold(host))){parts[i]=noteDomain(host);break;}
        }
        return String.join(",",parts);
    }
    static String urlHost(String value) {
        if(value.isEmpty() || value.codePoints().anyMatch(c->c<=32||c==127) || value.contains("\\"))throw ApiException.bad("URL格式错误");
        try {
            // Like urllib.urlsplit, inspect the authority only; a valid export may
            // contain literal braces or brackets in the URL path/query.
            java.util.regex.Matcher match=java.util.regex.Pattern.compile("(?is)^https?://([^/?#]*)(?:[/?#].*)?$").matcher(value);
            if(!match.matches())throw new Exception();
            String authority=match.group(1);
            if(authority.isEmpty() || authority.contains("@"))throw new Exception();
            String host;String port="";
            if(authority.startsWith("[")){
                int end=authority.indexOf(']');if(end<1)throw new Exception();host=authority.substring(1,end);
                if(end+1<authority.length()){if(authority.charAt(end+1)!=':')throw new Exception();port=authority.substring(end+2);}
                if(!host.matches("[0-9A-Fa-f:.]+") || !host.contains(":"))throw new Exception();
                byte[] bytes=InetAddress.getByName(host).getAddress(); // numeric IPv6 only; never DNS
                if(bytes.length==4) {
                    // InetAddress represents an IPv4-mapped IPv6 literal as IPv4.
                    byte[] mapped=new byte[16];mapped[10]=(byte)255;mapped[11]=(byte)255;
                    System.arraycopy(bytes,0,mapped,12,4);bytes=mapped;
                }
                host=ipv6(bytes);
            }else{
                int colon=authority.lastIndexOf(':');host=colon<0?authority:authority.substring(0,colon);port=colon<0?"":authority.substring(colon+1);
                host=host.replaceAll("\\.+$","").toLowerCase(Locale.ROOT);host=IDN.toASCII(host).toLowerCase(Locale.ROOT);
                if(host.length()>253 || host.isEmpty())throw new Exception();
                for(String label:host.split("\\.",-1))if(!label.matches("[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"))throw new Exception();
            }
            if(!port.isEmpty() && (!port.matches("[0-9]+") || Long.parseLong(port)<1 || Long.parseLong(port)>65535))throw new Exception();
            return host;
        }catch(Exception e){throw ApiException.bad("URL须为有效HTTP(S)地址，且不能含登录凭据");}
    }
    private static String ipv6(byte[] bytes) {
        if(bytes.length!=16)throw new IllegalArgumentException();int[] words=new int[8];for(int i=0;i<8;i++)words[i]=((bytes[2*i]&255)<<8)|(bytes[2*i+1]&255);
        int best=-1,length=0;for(int i=0;i<8;){if(words[i]!=0){i++;continue;}int j=i;while(j<8&&words[j]==0)j++;if(j-i>length){best=i;length=j-i;}i=j;}
        if(length<2)best=-1;StringBuilder out=new StringBuilder();
        for(int i=0;i<8;){if(i==best){out.append("::");i+=length;continue;}if(out.length()>0&&out.charAt(out.length()-1)!=':')out.append(':');out.append(Integer.toHexString(words[i++]));}
        return out.toString();
    }
    private static final class CsvRow {
        final List<String> fields;final int line;
        CsvRow(List<String> fields,int line){this.fields=fields;this.line=line;}
    }
    private static ApiException csvError(List<CsvRow> rows){return ApiException.bad(rows.isEmpty()?"CSV表头须包含url、username、password，且不能重复":"CSV格式错误（第"+rows.get(rows.size()-1).line+"行附近）");}
    /** Python csv strict dialect: commas, doubled quotes, CR/LF, quoted multiline cells. */
    static List<CsvRow> csvRows(String text) {
        List<CsvRow> rows=new ArrayList<>();List<String> fields=new ArrayList<>();StringBuilder cell=new StringBuilder();
        boolean quoted=false,afterQuote=false,started=false;int line=1;
        for(int i=0;i<text.length();i++) {
            char c=text.charAt(i);
            if(quoted) {
                if(c=='"'){if(i+1<text.length()&&text.charAt(i+1)=='"'){cell.append('"');i++;}else{quoted=false;afterQuote=true;}}
                else{cell.append(c);if(c=='\n'||c=='\r'){if(c=='\r'&&i+1<text.length()&&text.charAt(i+1)=='\n')cell.append(text.charAt(++i));line++;}}
                continue;
            }
            if(afterQuote && c!=',' && c!='\n' && c!='\r')throw csvError(rows);
            if(c==',' ){fields.add(cell.toString());cell.setLength(0);afterQuote=false;started=false;}
            else if(c=='\n'||c=='\r'){
                if(started || !fields.isEmpty() || cell.length()>0 || afterQuote){fields.add(cell.toString());rows.add(new CsvRow(fields,line));}
                else if(rows.isEmpty())rows.add(new CsvRow(Collections.emptyList(),line));
                fields=new ArrayList<>();cell.setLength(0);started=false;afterQuote=false;
                if(c=='\r'&&i+1<text.length()&&text.charAt(i+1)=='\n')i++;line++;
            }else if(c=='"'&&!started&&cell.length()==0){quoted=true;started=true;}
            else{cell.append(c);started=true;}
        }
        if(quoted)throw csvError(rows);
        if(started||!fields.isEmpty()||cell.length()>0||afterQuote){fields.add(cell.toString());rows.add(new CsvRow(fields,line));}
        return rows;
    }
    private static final class Group {
        String url;final List<String> notes=new ArrayList<>(),hosts=new ArrayList<>();final List<Integer> rows=new ArrayList<>();
        Group(String url){this.url=url;}
    }
    private static final class Errors {
        int count;final List<Map<String,Object>> values=new ArrayList<>();
        void add(int row,String message){count++;if(values.size()<20)values.add(Json.map("row",row,"message",message));}
    }
    static Plan csv(Object input,Object category,List<Map<String,Object>> entries) {
        if(!"web".equals(category))throw ApiException.bad("CSV导入仅支持网页分类");
        if(!(input instanceof String))throw ApiException.bad("请选择UTF-8 CSV文件");
        String text=(String)input;
        if(Json.utf8(text).length>MAX_CSV_BYTES)throw new ApiException(413,"CSV文件不能超过128 KiB");
        List<CsvRow> parsed=csvRows(text.replaceFirst("^\\uFEFF+",""));
        if(parsed.isEmpty())throw ApiException.bad("CSV表头须包含url、username、password，且不能重复");
        List<String> headers=parsed.get(0).fields.stream().map(s->Json.strip(s).toLowerCase(Locale.ROOT)).collect(Collectors.toList());
        if(headers.isEmpty()||headers.size()>16||new HashSet<>(headers).size()!=headers.size()||!headers.containsAll(Java8.list("url","username","password")))throw ApiException.bad("CSV表头须包含url、username、password，且不能重复");
        Map<Key,Group> groups=new LinkedHashMap<>();Errors errors=new Errors();int rows=0,skipped=0,skippedEmpty=0;
        List<Map<String,Object>> skippedEntries=new ArrayList<>();
        for(CsvRow row:parsed.subList(1,parsed.size())) {
            if(row.fields.stream().allMatch(String::isEmpty))continue;
            if(++rows>4096)throw new ApiException(413,"CSV不能超过4096条数据行");
            if(row.fields.size()!=headers.size()){errors.add(row.line,"列数与表头不一致");continue;}
            Map<String,String> v=new HashMap<>();for(int i=0;i<headers.size();i++)v.put(headers.get(i),row.fields.get(i));
            String user=v.get("username"),password=v.get("password"),url=Json.strip(v.get("url")),note=v.getOrDefault("note","");
            if(Json.strip(user).isEmpty()){skipped++;skippedEmpty++;continue;}
            if(!Entries.printable(user)||!Entries.printable(password)) {
                String field=!Entries.printable(user)?(!Entries.printable(password)?"账号和密码":"账号"):"密码";
                skippedEntries.add(Json.map("row",row.line,"message",field+"含蓝牙键盘不支持的字符，已跳过"));
                skipped++;continue;
            }
            try {
                if(Json.utf8(user).length>96)throw ApiException.bad("账号超过96字节的名称上限，不能直接用作名称");
                Entries.validate(Json.map("title",user,"username",user,"password",password,"target","","note",note,"category","web"),null);
                String host=urlHost(url);Key key=new Key("web",user,password);Group group=groups.computeIfAbsent(key,k->new Group(url));
                group.rows.add(row.line);if(!note.isEmpty()&&!group.notes.contains(note))group.notes.add(note);if(!group.hosts.contains(host))group.hosts.add(host);
            }catch(ApiException e){errors.add(row.line,e.getMessage());}
        }
        if(rows==0)throw ApiException.bad("CSV没有可导入的数据行");
        List<Map<String,Object>> result=new ArrayList<>(entries),preview=new ArrayList<>();Map<Key,Integer> existing=new HashMap<>();
        for(int i=0;i<entries.size();i++)if(entries.get(i).get("category").equals("web"))existing.putIfAbsent(Key.of(entries.get(i)),i);
        int added=0,updated=0,unchanged=0,merged=0;
        for(Map.Entry<Key,Group> pair:groups.entrySet()) {
            Key key=pair.getKey();Group g=pair.getValue();merged+=g.rows.size()-1;Integer index=existing.get(key);Map<String,Object> old=index==null?null:entries.get(index);
            String target=old!=null&&!old.get("target").equals("")?(String)old.get("target"):g.url;
            String primary="";try{primary=urlHost(target);}catch(ApiException ignored){}
            List<String> additions=new ArrayList<>();for(String value:g.notes)additions.add(compactKnownDomains(value,g.hosts));
            for(String host:g.hosts)if(!host.equals(primary))additions.add(noteDomain(host));
            String note=extendNote(old==null?"":compactKnownDomains((String)old.get("note"),g.hosts),additions);
            try {
                if(Json.utf8(target).length>192)throw ApiException.bad("首条URL超过192字节，请调整后再导入");
                Map<String,Object> entry=Entries.validate(Json.map("title",key.username,"username",key.username,"password",key.password,"target",target,"note",note,"category","web"),old);
                String action;
                if(old!=null){entry.put("favorite",old.getOrDefault("favorite",false));action=entry.equals(old)?"unchanged":"updated";result.set(index,entry);}else{action="added";result.add(entry);}
                if(action.equals("added"))added++;else if(action.equals("updated"))updated++;else unchanged++;
                Map<String,Object> meta=Entries.metadata(entry);meta.put("action",action);meta.put("source_rows",g.rows);preview.add(meta);
            }catch(ApiException e){errors.add(g.rows.get(0),e.getMessage());}
        }
        if(result.size()>VaultProtocol.MAX_ENTRIES)errors.add(0,"合并后共"+result.size()+"条，超过设备兼容上限"+VaultProtocol.MAX_ENTRIES+"条；本次不会写入");
        if(errors.count==0&&!groups.isEmpty())try{VaultProtocol.checkSize(result);}catch(ApiException e){errors.add(0,e.getMessage());}
        boolean canImport=errors.count==0&&!groups.isEmpty();
        boolean reordered=canImport&&Entries.sortWebByAccount(result);
        preview.sort(Entries.ACCOUNT_ORDER);
        return new Plan(result,Json.map("category","web","rows",rows,"skipped",skipped,"skipped_empty",skippedEmpty,"skipped_entries",skippedEntries,"unique",groups.size(),"merged",merged,"total",result.size(),"entries",preview,"errors",errors.values,"error_count",errors.count,"can_import",canImport,"added",added,"updated",updated,"unchanged",unchanged,"reordered",reordered));
    }
    static Archive readArchive(Object input,VaultProtocol protocol) {
        if(!(input instanceof String)||((String)input).length()>349528)throw ApiException.bad("请选择不超过256 KiB的加密密码文件");
        String text=(String)input;
        byte[] blob=null;
        try {
            if(text.length()%4!=0)throw new IllegalArgumentException();
            blob=Base64.getDecoder().decode(text);Map<String,Object> info=VaultProtocol.inspect(blob);List<Map<String,Object>> incoming=protocol.decode(blob),result=new ArrayList<>();
            for(Map<String,Object> raw:incoming){
                if(!raw.keySet().equals(Entries.FIELDS)||!(raw.get("id")instanceof String)||!((String)raw.get("id")).matches("[0-9a-f]{32}")||!(raw.get("favorite")instanceof Boolean)||!(raw.get("category")instanceof String))throw new IllegalArgumentException();
                String id=(String)raw.get("id");
                Map<String,Object> item=Entries.validate(raw,null);item.put("id",id);item.put("favorite",raw.get("favorite"));result.add(item);
            }
            return new Archive(result,((Number)info.get("revision")).longValue());
        }catch(Exception e){throw ApiException.bad("文件损坏、格式不兼容或部署密钥不匹配；没有导入任何条目");}
        finally{if(blob!=null)Arrays.fill(blob,(byte)0);}
    }
    static Plan archive(List<Map<String,Object>> incoming,List<Map<String,Object>> entries) {
        List<Map<String,Object>> result=new ArrayList<>();for(Map<String,Object> e:entries)result.add(new LinkedHashMap<>(e));
        Map<Key,Integer> indices=new HashMap<>(),touched=new LinkedHashMap<>();Set<String> used=new HashSet<>();
        for(int i=0;i<result.size();i++){indices.putIfAbsent(Key.of(result.get(i)),i);used.add((String)result.get(i).get("id"));}
        for(Map<String,Object> raw:incoming){Key key=Key.of(raw);Integer index=indices.get(key);Map<String,Object> item;
            if(index==null){item=new LinkedHashMap<>(raw);item.put("note",Entries.limitNote((String)item.get("note")));while(used.contains(item.get("id")))item.put("id",VaultProtocol.randomHex(16));index=result.size();result.add(item);used.add((String)item.get("id"));indices.put(key,index);}
            else{
                Map<String,Object> old=result.get(index);item=new LinkedHashMap<>(old);List<String> additions=new ArrayList<>();
                if(!raw.get("title").equals(old.get("title")))additions.add("名称："+raw.get("title"));
                if(old.get("target").equals(""))item.put("target",raw.get("target"));else if(!raw.get("target").equals("")&&!raw.get("target").equals(old.get("target")))additions.add("用途："+raw.get("target"));
                if(!raw.get("note").equals(""))additions.add((String)raw.get("note"));
                item.put("note",extendNote((String)old.get("note"),additions));item.put("favorite",Boolean.TRUE.equals(old.get("favorite"))||Boolean.TRUE.equals(raw.get("favorite")));
                try{Entries.validate(item,null);}catch(Exception e){throw ApiException.bad("去重合并后的字段无效；请调整条目后重新导入，整批未写入");}
                result.set(index,item);
            }
            touched.put(key,index);
        }
        if(result.size()>VaultProtocol.MAX_ENTRIES)throw ApiException.bad("去重后超过"+VaultProtocol.MAX_ENTRIES+"条设备容量上限；请减少条目，整批未写入");
        VaultProtocol.checkSize(result);
        int added=0,updated=0,unchanged=0;List<Map<String,Object>> preview=new ArrayList<>();
        for(int index:touched.values()){Map<String,Object> item=result.get(index);String action=index>=entries.size()?"added":item.equals(entries.get(index))?"unchanged":"updated";
            if(action.equals("added"))added++;else if(action.equals("updated"))updated++;else unchanged++;
            Map<String,Object> meta=Entries.metadata(item);meta.put("action",action);preview.add(meta);
        }
        return new Plan(result,Json.map("rows",incoming.size(),"unique",touched.size(),"merged",incoming.size()-touched.size(),"total",result.size(),"entries",preview,"can_import",true,"added",added,"updated",updated,"unchanged",unchanged));
    }
}
