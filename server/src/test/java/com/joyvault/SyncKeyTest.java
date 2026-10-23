package com.joyvault;

import org.junit.jupiter.api.*;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import java.nio.file.*;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;

/** Synthetic-only tests: never opens pc/data or the device. */
class SyncKeyTest {
    @TempDir Path dir;
    VaultStore store;
    AuthService auth;
    VaultController controller;
    DeploymentKeys keys;
    @BeforeEach void open() {
        keys=VaultCapacityImportTest.keys();
        Settings settings=new Settings(dir,dir.resolve("unused"),"http://127.0.0.1:8443",8443);
        store=new VaultStore(settings,keys);auth=new AuthService(store,settings);
        controller=new VaultController(store,auth,settings);
    }
    @AfterEach void close(){if(store!=null)store.close();}
    MockHttpServletRequest request(String proof) {
        MockHttpServletRequest r=new MockHttpServletRequest();
        r.addHeader("Authorization","Bearer "+keys.token);
        if(proof!=null)r.addHeader("X-Vault-Key",proof);
        return r;
    }
    void initialize(){auth.setup(Json.map("password","1234","confirm","1234"));}
    @Test void missingKeyCannotFallBackToBearer() {
        assertEquals(403,assertThrows(ApiException.class,()->auth.device(request(null))).status);
    }
    @Test void wrongEmptyMalformedAndRotatedKeysDenied() {
        for(String key:Java8.list("", "bad", String.join("",Collections.nCopies(64,"0")), BuiltinSyncKey.proof(new byte[32])))
            assertEquals(403,assertThrows(ApiException.class,()->auth.device(request(key))).status);
    }
    @Test void matchingCompiledKeyAcceptedButBearerStillRequired() {
        assertDoesNotThrow(()->auth.device(request(BuiltinSyncKey.proof())));
        MockHttpServletRequest r=request(BuiltinSyncKey.proof());r.removeHeader("Authorization");
        assertEquals(401,assertThrows(ApiException.class,()->auth.device(r)).status);
    }
    @Test void authenticationHappensBeforeReadingSnapshot()throws Exception {
        assertEquals(403,assertThrows(ApiException.class,()->controller.bundle(request("bad"),new MockHttpServletResponse())).status);
        assertEquals(404,assertThrows(ApiException.class,()->controller.bundle(request(BuiltinSyncKey.proof()),new MockHttpServletResponse())).status);
        assertEquals(403,assertThrows(ApiException.class,()->controller.manifest(request(null))).status);
    }
    @Test void eachSyncReturnsFullSnapshotAndAcknowledgesKey()throws Exception {
        initialize();
        store.transaction(()->store.save(Java8.list(VaultCapacityImportTest.entry("sample","work")),store.current().revision()));
        MockHttpServletRequest r=request(BuiltinSyncKey.proof());r.addHeader("If-None-Match","999999");r.addHeader("Range","bytes=0-5");
        for(int i=0;i<5;i++){
            MockHttpServletResponse response=new MockHttpServletResponse();controller.bundle(r,response);
            assertEquals(200,response.getStatus());assertEquals(BuiltinSyncKey.proof(),response.getHeader("X-Vault-Key"));
            assertArrayEquals(store.current().blob(),response.getContentAsByteArray());
            assertEquals(1,store.protocol.decode(response.getContentAsByteArray()).size());
        }
    }
    @Test void initializedEmptySnapshotIsValidButNeverImplicitlyCreated()throws Exception {
        initialize();
        MockHttpServletResponse response=new MockHttpServletResponse();controller.bundle(request(BuiltinSyncKey.proof()),response);
        assertEquals(0,store.protocol.decode(response.getContentAsByteArray()).size());
    }
    @Test void failedSyncDoesNotChangeDataOrAdministrator() {
        initialize();byte[] blob=store.current().blob(),pin=store.admin().hash();
        assertThrows(ApiException.class,()->auth.device(request("wrong")));
        assertArrayEquals(blob,store.current().blob());assertArrayEquals(pin,store.admin().hash());
    }
    @Test void externalSyncFieldCannotOverrideCompiledKey()throws Exception {
        Path file=dir.resolve("synthetic-keys.json");
        Files.write(file,Json.bytes(Json.map("schema",2,"key",Java8.hex(keys.key),"token",keys.token,"vault_id",Java8.hex(keys.id),"salt",Java8.hex(keys.salt),"deploy_key",String.join("",Collections.nCopies(64,"0")))));
        DeploymentKeys loaded=DeploymentKeys.load(file);
        assertArrayEquals(keys.key,loaded.key);
        assertDoesNotThrow(()->auth.device(request(BuiltinSyncKey.proof())));
        assertEquals(403,assertThrows(ApiException.class,()->auth.device(request(BuiltinSyncKey.proof(new byte[32])))).status);
    }
    @Test void keyRotationDoesNotChangeStorageEnvelopeOrPin() {
        initialize();byte[] blob=store.current().blob(),pin=store.admin().hash();
        byte[] first=new byte[32],second=new byte[32];Arrays.fill(second,(byte)7);
        assertNotEquals(BuiltinSyncKey.proof(first),BuiltinSyncKey.proof(second));
        VaultProtocol reopened=new VaultProtocol(new DeploymentKeys(keys.key,keys.token,keys.id,keys.salt));
        assertEquals(0,reopened.decode(blob).size());
        store.close();Settings settings=new Settings(dir,dir.resolve("unused"),"http://127.0.0.1:8443",8443);
        store=new VaultStore(settings,keys);
        assertArrayEquals(blob,store.current().blob());assertArrayEquals(pin,store.admin().hash());
    }
    @Test void proofDomainLengthIsExact() {
        assertEquals(VaultProtocol.sha256(join("JYVAULT-DEPLOY-KEY-V2".getBytes(java.nio.charset.StandardCharsets.US_ASCII),new byte[32])),BuiltinSyncKey.proof(new byte[32]));
        assertThrows(IllegalArgumentException.class,()->BuiltinSyncKey.proof(new byte[31]));
    }
    static byte[] join(byte[] a,byte[] b){byte[] v=Arrays.copyOf(a,a.length+b.length);System.arraycopy(b,0,v,a.length,b.length);return v;}
}
