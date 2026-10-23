package com.joyvault;

import org.junit.jupiter.api.*;
import static org.junit.jupiter.api.Assertions.*;
import org.springframework.mock.web.MockHttpServletRequest;
import javax.servlet.http.Cookie;
import java.nio.file.*;
import java.util.*;
import java.util.stream.*;

class CompatibilityTest {
    static Map<String,Object> fixtures;
    @BeforeAll static void load()throws Exception{fixtures=Json.object(Java8.read(CompatibilityTest.class.getResourceAsStream("/compatibility.json"),4*1024*1024));}
    @SuppressWarnings("unchecked") static Map<String,Object> object(Object o){return (Map<String,Object>)o;}
    @SuppressWarnings("unchecked") static List<Map<String,Object>> list(Object o){return (List<Map<String,Object>>)o;}
    static DeploymentKeys keys(){Map<String,Object> k=object(fixtures.get("keys"));return new DeploymentKeys(DeploymentKeys.hex(k,"key",32),(String)k.get("token"),DeploymentKeys.hex(k,"vault_id",16),DeploymentKeys.hex(k,"salt",16));}
    static Object canonical(Object value){
        if(value instanceof Map<?,?>){Map<?,?> m=(Map<?,?>)value;Map<String,Object> out=new LinkedHashMap<>();for(Map.Entry<?,?> e:m.entrySet())if(!e.getKey().equals("id"))out.put(e.getKey().toString(),canonical(e.getValue()));return out;}
        if(value instanceof List<?>)return ((List<?>)value).stream().map(CompatibilityTest::canonical).collect(Collectors.toList());return value;
    }
    static Object tree(Object value)throws Exception{return Json.MAPPER.readTree(Json.bytes(canonical(value)));}
    @TestFactory Stream<DynamicTest> csvCompatibility(){return list(fixtures.get("csv")).stream().map(c->DynamicTest.dynamicTest((String)c.get("name"),()->{
        Map<String,Object> expected=object(c.get("expected"));
        if(expected.containsKey("status")){ApiException e=assertThrows(ApiException.class,()->ImportService.csv(c.get("csv"),c.get("category"),list(c.get("existing"))));assertEquals(expected.get("status"),e.status);assertEquals(expected.get("detail"),e.getMessage());}
        else{ImportService.Plan plan=ImportService.csv(c.get("csv"),c.get("category"),list(c.get("existing")));assertEquals(tree(expected.get("entries")),tree(plan.entries()));assertEquals(tree(expected.get("report")),tree(plan.report()));}
    }));}
    @TestFactory Stream<DynamicTest> archiveCompatibility(){return list(fixtures.get("archives")).stream().map(c->DynamicTest.dynamicTest("archive: "+c.get("name"),()->{
        Map<String,Object> expected=object(c.get("expected"));
        if(expected.containsKey("status")){ApiException e=assertThrows(ApiException.class,()->ImportService.archive(list(c.get("incoming")),list(c.get("existing"))));assertEquals(expected.get("status"),e.status);assertEquals(expected.get("detail"),e.getMessage());}
        else{ImportService.Plan plan=ImportService.archive(list(c.get("incoming")),list(c.get("existing")));assertEquals(tree(expected.get("entries")),tree(plan.entries()));assertEquals(tree(expected.get("report")),tree(plan.report()));assertEquals(plan.entries().size(),plan.entries().stream().map(e->e.get("id")).distinct().count());}
    }));}
    @TestFactory Stream<DynamicTest> protocolVectors(){return list(fixtures.get("vectors")).stream().map(c->DynamicTest.dynamicTest("Python AEAD r"+c.get("revision"),()->{
        VaultProtocol p=new VaultProtocol(keys());byte[] blob=Base64.getDecoder().decode((String)c.get("blob"));
        assertEquals(tree(c.get("entries")),tree(p.decode(blob)));assertEquals(((Number)c.get("revision")).longValue(),VaultProtocol.inspect(blob).get("revision"));
        byte[] java=p.encode(list(c.get("entries")),((Number)c.get("revision")).longValue());assertEquals(tree(c.get("entries")),tree(p.decode(java)));
        for(int offset:new int[]{8,12,16,20,24,40,56,68,115,116,128,blob.length-1}){byte[] bad=blob.clone();bad[offset]^=1;assertThrows(RuntimeException.class,()->p.decode(bad));}
        DeploymentKeys other=keys();other.key[0]^=1;assertThrows(RuntimeException.class,()->new VaultProtocol(other).decode(blob));
    }));}
    @TestFactory Stream<DynamicTest> passwordHashes(){return list(fixtures.get("password_hashes")).stream().map(c->DynamicTest.dynamicTest("Python PBKDF2 UTF8 "+((String)c.get("password")).codePointCount(0,((String)c.get("password")).length()),()->{
        assertEquals(c.get("hash"),Java8.hex(AuthService.passwordHash((String)c.get("password"),Java8.unhex((String)c.get("salt")))));
    }));}
    @Test void validationAndLimits(){
        Map<String,Object> sample=list(list(fixtures.get("vectors")).get(1).get("entries")).get(0);
        for(Map.Entry<String,Object> field:Json.map("username","中文","password","\n","title"," ","note","invalid\nnote","target",String.join("",Collections.nCopies(193,"x"))).entrySet()){
            Map<String,Object> bad=new LinkedHashMap<>(sample);bad.put(field.getKey(),field.getValue());assertThrows(ApiException.class,()->Entries.validate(bad,null));
        }
        Map<String,Object> bad=new LinkedHashMap<>(sample);bad.put("title","\ud800");assertThrows(ApiException.class,()->Entries.validate(bad,null));
        assertFalse(Json.integerEquals(true,1));assertFalse(Json.integerEquals(1.0,1));assertTrue(Json.integerEquals(1,1));
        assertEquals(Java8.list("work","app","web","other"),new ArrayList<>(Entries.CATEGORIES.keySet()));
    }
    @Test void atomicPersistenceAndOriginalKeysRequired()throws Exception{
        Path dir=Paths.get("target/test-data/"+UUID.randomUUID()).toAbsolutePath();Settings settings=new Settings(dir,dir.resolve("not-used"),"http://127.0.0.1:8443",8443);List<Map<String,Object>> sample=list(list(fixtures.get("vectors")).get(1).get("entries"));
        byte[] blob;
        try(varClose store=new varClose(new VaultStore(settings,keys()))){
            store.value.transaction(()->{store.value.save(sample,0);return null;});blob=store.value.current().blob();
            assertThrows(RuntimeException.class,()->store.value.transaction(()->{store.value.save(Java8.list(),1);throw new IllegalStateException("synthetic rollback");}));
            assertArrayEquals(blob,store.value.current().blob());assertTrue(store.value.healthy());
            assertThrows(RuntimeException.class,()->new VaultStore(settings,keys()));
        }
        try(varClose reopened=new varClose(new VaultStore(settings,keys()))){assertArrayEquals(blob,reopened.value.current().blob());}
        byte[] before=Files.readAllBytes(dir.resolve("vault.db"));DeploymentKeys wrong=keys();wrong.key[0]^=1;
        assertThrows(RuntimeException.class,()->new VaultStore(settings,wrong));assertArrayEquals(before,Files.readAllBytes(dir.resolve("vault.db")));
        assertFalse(new String(before,java.nio.charset.StandardCharsets.ISO_8859_1).contains("Test-Only!4"));
    }
    @Test void sessionsAndPasswordChangeAreIndependentFromVault()throws Exception{
        Path dir=Paths.get("target/test-data/"+UUID.randomUUID()).toAbsolutePath();Settings settings=new Settings(dir,dir.resolve("none"),"http://127.0.0.1:8443",8443);
        try(varClose close=new varClose(new VaultStore(settings,keys()))){VaultStore store=close.value;AuthService auth=new AuthService(store,settings);
            org.springframework.http.ResponseEntity<Map<String,Object>> response=auth.setup(Json.map("password","1234","confirm","1234"));String sid=response.getHeaders().getFirst("Set-Cookie").split(";",2)[0].substring(3);
            MockHttpServletRequest r=new MockHttpServletRequest();r.setRemoteAddr("127.0.0.1");r.setCookies(new Cookie("jv",sid));auth.require(r);
            VaultStore.Snapshot snapshot=store.current();assertThrows(ApiException.class,()->auth.setup(Json.map("password","other","confirm","other")));
            org.springframework.http.ResponseEntity<Map<String,Object>> changed=auth.change(r,Json.map("password","1234","new_password","四字新密","confirm","四字新密"));
            assertArrayEquals(snapshot.blob(),store.current().blob());assertThrows(ApiException.class,()->auth.require(r));
            String newSid=changed.getHeaders().getFirst("Set-Cookie").split(";",2)[0].substring(3);r.setCookies(new Cookie("jv",newSid));auth.require(r);auth.verify(r,"四字新密",null);
            ApiException bad=assertThrows(ApiException.class,()->auth.verify(r,"wrong",null));assertEquals(401,bad.status);
            assertEquals(429,assertThrows(ApiException.class,()->auth.verify(r,"四字新密",null)).status);auth.require(r);
        }
    }
    @Test void commitFailureRollsBackBeforeAutoCommit()throws Exception{
        Path dir=Paths.get("target/test-data/"+UUID.randomUUID()).toAbsolutePath();
        Settings settings=new Settings(dir,dir.resolve("unused"),"http://127.0.0.1:8443",8443);
        try(varClose close=new varClose(new VaultStore(settings,keys()))){
            VaultStore store=close.value;
            store.transaction(()->{store.save(Java8.list(),0);return null;});
            byte[] before=store.current().blob();
            java.lang.reflect.Field field=VaultStore.class.getDeclaredField("db");field.setAccessible(true);
            java.sql.Connection original=(java.sql.Connection)field.get(store);
            java.util.concurrent.atomic.AtomicBoolean failed=new java.util.concurrent.atomic.AtomicBoolean();
            java.sql.Connection connection=(java.sql.Connection)java.lang.reflect.Proxy.newProxyInstance(
                getClass().getClassLoader(),new Class<?>[]{java.sql.Connection.class},(proxy,method,args)->{
                    if(method.getName().equals("commit") && failed.compareAndSet(false,true))
                        throw new java.sql.SQLException("Synthetic commit failure");
                    try{return method.invoke(original,args);}
                    catch(java.lang.reflect.InvocationTargetException e){throw e.getCause();}
                });
            field.set(store,connection);
            assertThrows(IllegalStateException.class,()->store.transaction(()->{store.save(Java8.list(),1);return null;}));
            assertArrayEquals(before,store.current().blob());assertTrue(store.healthy());
            store.transaction(()->{store.save(Java8.list(),1);return null;});assertEquals(2,store.current().revision());
        }
    }
    @Test void publicSchemeMustMatchActualTls(){
        org.springframework.mock.env.MockEnvironment env=new org.springframework.mock.env.MockEnvironment().withProperty("VAULT_PUBLIC_URL","https://127.0.0.1:8443");
        assertThrows(IllegalArgumentException.class,()->Settings.from(env));
        env.withProperty("server.ssl.enabled","true");assertEquals(8443,Settings.from(env).port());
        env.withProperty("VAULT_PUBLIC_URL","http://127.0.0.1:8443");
        assertThrows(IllegalArgumentException.class,()->Settings.from(env));
    }
    @Test void pythonWhitespaceAndStrictArchivePadding(){
        for(String text:Java8.list("\u00a0","\u0085","\u2007","\u202f")){
            assertTrue(Json.strip(text).isEmpty());
            assertThrows(ApiException.class,()->Entries.validate(Json.map("title",text,"password","Test!","category","work"),null));
        }
        assertEquals("example.com",ImportService.urlHost("https://example.com/{user}?q=[value]"));
        assertEquals("::ffff:c000:201",ImportService.urlHost("https://[::ffff:192.0.2.1]/a"));
        String blob=Base64.getEncoder().encodeToString(keysProtocol().encode(Java8.list(),1));
        // Find a legal padded archive, then removing required padding must be rejected.
        assertTrue(blob.endsWith("="));assertThrows(ApiException.class,()->ImportService.readArchive(blob.replaceAll("=+$",""),keysProtocol()));
    }
    private VaultProtocol keysProtocol(){return new VaultProtocol(keys());}
    private static final class varClose implements AutoCloseable{final VaultStore value;varClose(VaultStore value){this.value=value;}public void close(){value.close();}}
}
