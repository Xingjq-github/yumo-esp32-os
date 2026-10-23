package com.joyvault;

import org.junit.jupiter.api.*;
import static org.junit.jupiter.api.Assertions.*;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.http.ResponseEntity;
import javax.servlet.http.Cookie;
import java.nio.file.*;
import java.lang.reflect.Field;
import java.util.*;

class SuperModeTest {
    VaultStore store;AuthService auth;VaultController controller;MockHttpServletRequest request;
    @BeforeEach void create() {
        Path dir=Paths.get("target/test-data/"+UUID.randomUUID()).toAbsolutePath();
        Settings settings=new Settings(dir,dir.resolve("unused"),"http://127.0.0.1:8443",8443);
        DeploymentKeys keys=new DeploymentKeys(VaultProtocol.random(32),VaultProtocol.randomHex(32),VaultProtocol.random(16),VaultProtocol.random(16));
        store=new VaultStore(settings,keys);auth=new AuthService(store,settings);controller=new VaultController(store,auth,settings);
        request=session(auth.setup(Json.map("password","1234","confirm","1234")));
        store.transaction(()->{store.save(Java8.list(Entries.validate(Json.map("category","web","title","sample","username","user","password","Only-Test!9","target","https://sample.test"),null)),store.current().revision());return null;});
    }
    @AfterEach void close(){if(store!=null)store.close();}
    static MockHttpServletRequest session(ResponseEntity<Map<String,Object>> response){
        MockHttpServletRequest r=new MockHttpServletRequest();r.setRemoteAddr("127.0.0.1");
        r.setCookies(new Cookie("jv",response.getHeaders().getFirst("Set-Cookie").split(";",2)[0].substring(3)));return r;
    }
    void enable(){auth.superMode(request,Json.map("enabled",true,"password","1234"));}
    @SuppressWarnings("unchecked") static List<Map<String,Object>> rows(Map<String,Object> response){return (List<Map<String,Object>>)response.get("entries");}
    @Test void permissionIsSessionBoundAndDoesNotChangeStoredVault(){
        byte[] before=store.current().blob();MockHttpServletRequest other=session(auth.login(request,Json.map("password","1234")));
        assertFalse(auth.superMode(request));assertFalse(rows(controller.entries(request)).get(0).containsKey("password"));
        enable();assertTrue(auth.superMode(request));assertFalse(auth.superMode(other));
        assertEquals("Only-Test!9",rows(controller.entries(request)).get(0).get("password"));
        assertFalse(rows(controller.entries(other)).get(0).containsKey("password"));
        auth.superMode(request,Json.map("enabled",false));assertFalse(auth.superMode(request));
        assertFalse(rows(controller.entries(request)).get(0).containsKey("password"));assertArrayEquals(before,store.current().blob());
    }
    @Test void passwordRequiredAndAnonymousCannotEnable(){
        assertEquals(401,assertThrows(ApiException.class,()->auth.superMode(new MockHttpServletRequest(),Json.map("enabled",true,"password","1234"))).status);
        assertEquals(400,assertThrows(ApiException.class,()->auth.superMode(request,Json.map("enabled","true"))).status);
        assertEquals(401,assertThrows(ApiException.class,()->auth.superMode(request,Json.map("enabled",true,"password","wrong"))).status);
        assertFalse(auth.superMode(request));
    }
    @Test void lockAndNewLoginAlwaysRemoveMode(){
        enable();auth.logout(request);assertEquals(401,assertThrows(ApiException.class,()->auth.superMode(request)).status);
        request=session(auth.login(request,Json.map("password","1234")));assertFalse(auth.superMode(request));
    }
    @Test void passwordChangeAndExpiryRevokeMode()throws Exception{
        enable();request=session(auth.change(request,Json.map("password","1234","new_password","5678","confirm","5678")));
        assertFalse(auth.superMode(request));auth.superMode(request,Json.map("enabled",true,"password","5678"));
        Field sessions=AuthService.class.getDeclaredField("sessions");sessions.setAccessible(true);
        Object state=((Map<?,?>)sessions.get(auth)).get(AuthService.cookie(request));
        Field until=state.getClass().getDeclaredField("until");until.setAccessible(true);until.setLong(state,System.nanoTime()-1);
        assertEquals(401,assertThrows(ApiException.class,()->controller.entries(request)).status);
    }
    @Test void importReportsRevealOnlyRequestedModeWithoutMutatingPlanner(){
        ImportService.Plan csv=ImportService.csv("url,username,password\nhttps://new.test,user,Incoming!9\n","web",store.read().entries());
        assertFalse(rows(csv.report()).get(0).containsKey("password"));
        assertEquals("Incoming!9",rows(csv.report(true)).get(0).get("password"));
        assertFalse(rows(csv.report(false)).get(0).containsKey("password"));
        ImportService.Plan archive=ImportService.archive(store.read().entries(),Java8.list());
        assertEquals("Only-Test!9",rows(archive.report(true)).get(0).get("password"));
        assertFalse(rows(archive.report()).get(0).containsKey("password"));
    }
}
