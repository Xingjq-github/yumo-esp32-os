package com.joyvault;

import org.junit.jupiter.api.Test;
import org.springframework.mock.web.MockHttpServletRequest;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;

class CodeValueTest extends SuperModeTest {
    @Test void codeIsVisibleOnlyAfterLoginWithoutRequiringSuperMode() {
        store.transaction(()->{
            List<Map<String,Object>> entries=new ArrayList<>(store.read().entries());
            entries.add(Entries.validate(Json.map("category","other","title","Door code","username","ignored","password","123456"),null));
            store.save(entries,store.current().revision());return null;
        });
        assertEquals(401,assertThrows(ApiException.class,()->controller.entries(new MockHttpServletRequest())).status);
        List<Map<String,Object>> entries=rows(controller.entries(request));
        assertFalse(entries.get(0).containsKey("password"));
        assertEquals("123456",entries.get(1).get("password"));
        assertFalse(entries.get(1).containsKey("username"));
        enable();auth.superMode(request,Json.map("enabled",false));
        assertEquals("123456",rows(controller.entries(request)).get(1).get("password"));
        auth.logout(request);
        assertEquals(401,assertThrows(ApiException.class,()->controller.entries(request)).status);
    }
    @Test void legacyAccountSurvivesEditingWithoutBecomingVisible() {
        Map<String,Object> old=Entries.validate(Json.map("category","work","title","Old","username","legacy","password","Old-Code!"),null);
        old.put("category","other");
        Map<String,Object> updated=Entries.validate(Json.map("category","other","title","Updated","password","New-Code!"),old);
        assertEquals("legacy",updated.get("username"));
        assertEquals("New-Code!",updated.get("password"));
        assertFalse(Entries.visible(updated,false).containsKey("username"));
        assertFalse(Entries.visible(updated,true).containsKey("username"));
        assertEquals("New-Code!",Entries.visible(updated,false).get("password"));
        assertEquals("Old-Code!",old.get("password"));
    }
    @Test void archiveCodePreviewUsesSameVisibilityWithoutChangingImportIdentity() {
        Map<String,Object> code=Entries.validate(Json.map("category","other","title","Code","password","Code!4"),null);
        ImportService.Plan plan=ImportService.archive(Java8.list(code),Java8.list());
        assertEquals("Code!4",rows(plan.report(false)).get(0).get("password"));
        assertFalse(rows(plan.report(true)).get(0).containsKey("username"));
        assertEquals("",plan.entries().get(0).get("username"));
    }
}
