package com.joyvault;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mock.web.MockHttpServletRequest;
import javax.servlet.http.Cookie;
import java.nio.file.Path;
import java.util.*;
import java.util.stream.Collectors;
import static org.junit.jupiter.api.Assertions.*;

class VaultCapacityImportTest {
    @TempDir Path dir;
    static DeploymentKeys keys() {
        byte[] key=new byte[32],id=new byte[16],salt=new byte[16];
        Arrays.fill(key,(byte)1);Arrays.fill(id,(byte)2);Arrays.fill(salt,(byte)3);
        return new DeploymentKeys(key,String.join("",Collections.nCopies(64,"4")),id,salt);
    }
    static Map<String,Object> entry(String user,String category) {
        return Entries.validate(Json.map("title",user,"username",user,"password","Synthetic!4",
            "target","https://first.example/a","note","","category",category),null);
    }
    static List<Map<String,Object>> entries(int n) {
        List<Map<String,Object>> result=new ArrayList<>();
        for(int i=0;i<n;i++)result.add(entry(String.format(Locale.ROOT,"user%03d",i),"web"));
        return result;
    }
    static List<String> users(List<Map<String,Object>> items) {
        return items.stream().map(e->(String)e.get("username")).collect(Collectors.toList());
    }
    @Test void accepts256AndRejects257WithoutChangingEnvelopeOrFieldLimits() {
        VaultProtocol protocol=new VaultProtocol(keys());List<Map<String,Object>> items=entries(256);
        byte[] blob=protocol.encode(items,1);
        assertEquals(256,VaultProtocol.MAX_ENTRIES);assertEquals(256,protocol.decode(blob).size());
        assertTrue(blob.length<VaultProtocol.MAX_BUNDLE);assertEquals(2,VaultProtocol.inspect(blob).get("schema"));
        items.add(entry("overflow","web"));assertThrows(IllegalArgumentException.class,()->protocol.encode(items,2));
        Map<String,Object> longPassword=entry("length","web");longPassword.put("password",String.join("",Collections.nCopies(129,"x")));
        assertThrows(ApiException.class,()->Entries.validate(longPassword,null));
    }
    @Test void skipsMissingAccountsBeforePasswordAndUrlValidation() {
        String csv="url,username,password\ninvalid,,\ninvalid,   ,\ninvalid,\u00a0,\nhttps://b.example,zeta,Synthetic!4\nhttps://a.example,Alpha,Synthetic!4\nhttps://a2.example,Alpha,Synthetic!4\n";
        ImportService.Plan plan=ImportService.csv(csv,"web",Collections.emptyList());
        assertEquals(3,plan.report().get("skipped"));assertEquals(6,plan.report().get("rows"));
        assertEquals(1,plan.report().get("merged"));assertEquals(0,plan.report().get("error_count"));
        assertEquals(true,plan.report().get("can_import"));assertEquals(Java8.list("Alpha","zeta"),users(plan.entries()));
        assertEquals("Alpha",plan.entries().get(0).get("title"));
        assertEquals("https://a.example",plan.entries().get(0).get("target"));
        assertEquals("a2.example",plan.entries().get(0).get("note"));
        assertFalse(Json.MAPPER.valueToTree(plan.report()).toString().contains("Synthetic!4"));
    }
    @Test void noAccountsIsANoOpAndInvalidNonemptyAccountsStillFail() {
        List<Map<String,Object>> existing=Java8.list(entry("z","web"),entry("a","web"));
        ImportService.Plan empty=ImportService.csv("url,username,password\ninvalid,,\ninvalid,   ,\n","web",existing);
        assertEquals(2,empty.report().get("skipped"));assertEquals(false,empty.report().get("can_import"));
        assertEquals(0,empty.report().get("error_count"));assertEquals(existing,empty.entries());
        ImportService.Plan invalid=ImportService.csv("url,username,password\ninvalid,,\ninvalid,real,Synthetic!4\n","web",existing);
        assertEquals(1,invalid.report().get("skipped"));assertEquals(1,invalid.report().get("error_count"));
        assertEquals(false,invalid.report().get("can_import"));
    }
    @Test void sortingPreservesOtherCategoriesAndExactCredentials() {
        List<Map<String,Object>> existing=Java8.list(entry("zeta","web"),entry("middle","work"),entry("beta","web"));
        String csv="url,username,password\nhttps://a.example,Alpha,Synthetic!4\nhttps://b.example,alpha,Synthetic!4\n";
        ImportService.Plan plan=ImportService.csv(csv,"web",existing);
        assertEquals(Java8.list("Alpha","middle","alpha","beta","zeta"),users(plan.entries()));
        assertSame(existing.get(1),plan.entries().get(1));assertEquals(Java8.list("zeta","middle","beta"),users(existing));
        ImportService.Plan repeated=ImportService.csv(csv,"web",plan.entries());
        assertEquals(false,repeated.report().get("reordered"));assertEquals(0,repeated.report().get("added"));
        assertEquals(plan.entries(),repeated.entries());
    }
    @Test void capacityCountsAcceptedUniqueRecordsAfterSkippingAndMerging() {
        List<Map<String,Object>> existing=entries(255);
        String csv="url,username,password\ninvalid,,\nhttps://new.example/new,new,Synthetic!4\nhttps://new.example/new,new,Synthetic!4\n";
        ImportService.Plan plan=ImportService.csv(csv,"web",existing);
        assertEquals(256,plan.entries().size());assertEquals(true,plan.report().get("can_import"));
        ImportService.Plan overflow=ImportService.csv(csv+"https://overflow.example,overflow,Synthetic!4\n","web",existing);
        assertEquals(false,overflow.report().get("can_import"));assertEquals(257,overflow.report().get("total"));
        assertEquals(255,existing.size());
    }
    @Test void orderOnlyImportIsPersistedAndRepeatDoesNotRewrite() {
        Settings settings=new Settings(dir,dir.resolve("unused"),"http://127.0.0.1:8443",8443);
        long revision;
        VaultStore store=new VaultStore(settings,keys());
        try {
            AuthService auth=new AuthService(store,settings);
            String cookie=auth.setup(Json.map("password","1234","confirm","1234")).getHeaders().getFirst("Set-Cookie").split(";",2)[0].substring(3);
            MockHttpServletRequest request=new MockHttpServletRequest();request.setRemoteAddr("127.0.0.1");request.setCookies(new Cookie("jv",cookie));
            store.transaction(()->{store.save(Java8.list(entry("zeta","web"),entry("alpha","web")),store.read().revision());return null;});
            VaultController controller=new VaultController(store,auth,settings);
            String csv="url,username,password\nhttps://first.example/a,zeta,Synthetic!4\n";
            long before=store.read().revision();byte[] snapshot=store.current().blob();
            request.setAttribute(BodyGuard.BODY,Json.bytes(Json.map("csv",csv,"category","web","preview",true)));
            Map<String,Object> preview=controller.csv(request);assertEquals(true,preview.get("reordered"));assertArrayEquals(snapshot,store.current().blob());
            request.setAttribute(BodyGuard.BODY,Json.bytes(Json.map("csv",csv,"category","web","preview",false,"revision",before)));
            Map<String,Object> commit=controller.csv(request);assertEquals(before+1,commit.get("revision"));
            assertEquals(0,commit.get("added"));assertEquals(0,commit.get("updated"));
            revision=store.read().revision();assertEquals(Java8.list("alpha","zeta"),users(store.read().entries()));
            request.setAttribute(BodyGuard.BODY,Json.bytes(Json.map("csv",csv,"category","web","preview",false,"revision",revision)));
            assertEquals(revision,controller.csv(request).get("revision"));
            assertEquals(256,controller.entries(request).get("max_entries"));
        } finally { store.close(); }
        VaultStore reopened=new VaultStore(settings,keys());
        try {
            assertEquals(revision,reopened.read().revision());assertEquals(Java8.list("alpha","zeta"),users(reopened.read().entries()));
        } finally { reopened.close(); }
    }
}
