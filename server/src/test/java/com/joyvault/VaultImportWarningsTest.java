package com.joyvault;

import org.junit.jupiter.api.Test;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;

class VaultImportWarningsTest {
    static String repeat(String s,int count){return String.join("",Collections.nCopies(count,s));}
    @Test void skipsUnsupportedFieldsWithRowsButNeverLeaksOrChangesCredentials() {
        String csv="url,username,password\nhttps://z.example,zeta,Synthetic!4\ninvalid,中文,Synthetic!4\ninvalid,ascii,Secret中!\ninvalid,\"tab\tuser\",Secret-Test\ninvalid,,\nhttps://a.example,Alpha,Synthetic!4\n";
        ImportService.Plan plan=ImportService.csv(csv,"web",Collections.emptyList());
        assertEquals(true,plan.report().get("can_import"));assertEquals(0,plan.report().get("error_count"));
        assertEquals(4,plan.report().get("skipped"));assertEquals(1,plan.report().get("skipped_empty"));
        assertEquals(Java8.list("Alpha","zeta"),VaultCapacityImportTest.users(plan.entries()));
        assertEquals(Java8.list(Json.map("row",3,"message","账号含蓝牙键盘不支持的字符，已跳过"),Json.map("row",4,"message","密码含蓝牙键盘不支持的字符，已跳过"),Json.map("row",5,"message","账号含蓝牙键盘不支持的字符，已跳过")),plan.report().get("skipped_entries"));
        String report=Json.MAPPER.valueToTree(plan.report()).toString();assertFalse(report.contains("Secret"));assertFalse(report.contains("Synthetic!4"));
        assertEquals("Synthetic!4",plan.entries().get(0).get("password"));
        ImportService.Plan only=ImportService.csv("url,username,password\ninvalid,a,中\n","web",plan.entries());
        assertEquals(false,only.report().get("can_import"));assertEquals(plan.entries(),only.entries());
    }
    @Test void domainsAreShortenedDeduplicatedAndTotalNoteIsClipped() {
        StringBuilder csv=new StringBuilder("url,username,password\nhttps://first.example/a,user,Synthetic!4\n");
        List<String> domains=new ArrayList<>();
        for(int i=0;i<70;i++){String domain="website"+i+".example";domains.add(domain.substring(0,10));for(int j=0;j<2;j++)csv.append("https://").append(domain).append("/path,user,Synthetic!4\n");}
        ImportService.Plan plan=ImportService.csv(csv.toString(),"web",Collections.emptyList());
        assertEquals(true,plan.report().get("can_import"));assertEquals(String.join(",",domains).substring(0,256),plan.entries().get(0).get("note"));
        assertEquals(256,Json.utf8((String)plan.entries().get(0).get("note")).length);
        assertEquals("https://first.example/a",plan.entries().get(0).get("target"));
        VaultProtocol protocol=new VaultProtocol(VaultCapacityImportTest.keys());
        assertEquals(plan.entries(),protocol.decode(protocol.encode(plan.entries(),1)));
        assertEquals(plan.entries(),ImportService.csv(csv.toString(),"web",plan.entries()).entries());
    }
    @Test void noteLimitUsesUtf8BytesWithoutDamagingCharactersOrCredentials() {
        Map<String,Object> entry=VaultCapacityImportTest.entry("user","web");
        String note=repeat("中",85)+"a";assertEquals(256,Json.utf8(note).length);
        entry.put("note",note);assertEquals(note,Entries.validate(entry,null).get("note"));
        entry.put("note",note+"中");Map<String,Object> clipped=Entries.validate(entry,null);
        assertEquals(note,clipped.get("note"));assertEquals(entry.get("password"),clipped.get("password"));assertEquals(entry.get("username"),clipped.get("username"));
        assertEquals(repeat("x",255),Entries.limitNote(repeat("x",255)+"中"));
        assertEquals(repeat("x",252)+"😀",Entries.limitNote(repeat("x",252)+"😀more"));
        assertEquals(repeat("x",253),Entries.limitNote(repeat("x",253)+"😀"));
        assertThrows(ApiException.class,()->Entries.limitNote("\ud800"));
        assertEquals(repeat("x",255),ImportService.extendNote(repeat("x",255),Java8.list("中文","a")));
    }
    @Test void shortenedCollisionsAndExistingNotesDoNotGrowOnRepeat() {
        String csv="url,username,password\nhttps://first.example/a,user,Synthetic!4\nhttps://abcdefghij-one.example/path,user,Synthetic!4\nhttps://abcdefghij-two.example/path,user,Synthetic!4\nhttps://short.io/path,user,Synthetic!4\n";
        ImportService.Plan plan=ImportService.csv(csv,"web",Collections.emptyList());
        assertEquals("abcdefghij,short.io",plan.entries().get(0).get("note"));
        assertEquals(plan.entries(),ImportService.csv(csv,"web",plan.entries()).entries());
        Map<String,Object> old=new LinkedHashMap<>(plan.entries().get(0));old.put("note","keep,abcdefghij-one.example");
        assertEquals("keep,abcdefghij,short.io",ImportService.csv(csv,"web",Java8.list(old)).entries().get(0).get("note"));
        String longCsv="url,username,password,note\nhttps://first.example/a,user,Synthetic!4,"+repeat("中",100)+"\n";
        ImportService.Plan truncated=ImportService.csv(longCsv,"web",Collections.emptyList());
        assertEquals(true,truncated.report().get("can_import"));assertEquals(repeat("中",85),truncated.entries().get(0).get("note"));
        assertEquals(truncated.entries(),ImportService.csv(longCsv,"web",truncated.entries()).entries());
    }
    @Test void archiveNotesAreClippedOnNewAndMergedRecords() {
        Map<String,Object> original=VaultCapacityImportTest.entry("user","web"),incoming=new LinkedHashMap<>(original);
        original.put("note","old");incoming.put("note",repeat("n",4096));
        assertEquals("old,"+repeat("n",252),ImportService.archive(Java8.list(incoming),Java8.list(original)).entries().get(0).get("note"));
        assertEquals(repeat("n",256),ImportService.archive(Java8.list(incoming),Collections.emptyList()).entries().get(0).get("note"));
        assertEquals(repeat("n",4096),incoming.get("note"));
    }
}
