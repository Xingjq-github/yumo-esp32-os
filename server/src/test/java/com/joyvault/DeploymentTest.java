package com.joyvault;
import java.util.*;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;
class DeploymentTest {
    @Test void probeDefaultsToNativeOrigin(){assertEquals("http://vault.example:8443/healthz",HealthProbe.endpoint(Java8.map("VAULT_PUBLIC_URL","http://vault.example:8443/")).toString());}
    @Test void probeUsesInternalOriginWithoutHairpin(){assertEquals("http://127.0.0.1:8443/healthz",HealthProbe.endpoint(Java8.map("VAULT_PUBLIC_URL","http://nas.invalid:8443","VAULT_HEALTH_URL","http://127.0.0.1:8443")).toString());}
    @Test void probeFallbackAndValidation(){assertEquals("http://127.0.0.1:8443/healthz",HealthProbe.endpoint(Java8.map()).toString());assertThrows(IllegalArgumentException.class,()->HealthProbe.endpoint(Java8.map("VAULT_HEALTH_URL","http://127.0.0.1:8443/wrong")));}
    Map<String,Object> body(){return Json.map("title","测试条目","username","test","password","test!","target","局域网","note","中文备注","category","work");}
    @Test void all95PrintableCharactersRoundtrip(){Map<String,Object> b=body();StringBuilder text=new StringBuilder();for(int c=32;c<127;c++)text.append((char)c);b.put("username",text.toString());b.put("password",text.toString());Map<String,Object> result=Entries.validate(b,null);assertEquals(text.toString(),result.get("username"));assertEquals(text.toString(),result.get("password"));}
    @Test void boundarySpacesAndPasswordLength(){Map<String,Object> b=body();String text="  Aa0!\"\\'`~{}[]|<>/?=+_-;:@#$%^&*()  ";b.put("password",text);assertEquals(text,Entries.validate(b,null).get("password"));b.put("password",String.join("",Collections.nCopies(128,"x")));assertEquals(128,((String)Entries.validate(b,null).get("password")).length());b.put("password",String.join("",Collections.nCopies(129,"x")));assertThrows(ApiException.class,()->Entries.validate(b,null));}
    @Test void chineseMetadataPreserved(){Map<String,Object> result=Entries.validate(body(),null);assertEquals("测试条目",result.get("title"));assertEquals("中文备注",result.get("note"));}
    @Test void unsupportedCredentialsRejected(){for(String field:Java8.list("username","password"))for(String text:Java8.list("中文","ＡＢＣ","“password”","🙂","test\n","test\t","test\u0000","test\u007f")){Map<String,Object> b=body();b.put(field,text);assertThrows(ApiException.class,()->Entries.validate(b,null),field);}}
}
