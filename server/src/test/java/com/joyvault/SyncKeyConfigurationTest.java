package com.joyvault;

import org.junit.jupiter.api.Test;
import java.io.Reader;
import java.io.StringReader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import static org.junit.jupiter.api.Assertions.*;

class SyncKeyConfigurationTest {
    private static final String HEX=String.join("",java.util.Collections.nCopies(32,"aB"));
    @Test void acceptsBomCommentsWhitespaceAndWindowsNewlines() throws Exception {
        byte[] key=BuiltinSyncKey.parseKey(new StringReader("\uFEFF# settings\r\n\r\n sync.key.hex \t=\t"+HEX+" \r\n"));
        assertEquals(32,key.length);
        for(byte value:key)assertEquals((byte)0xab,value);
    }
    @Test void rejectsMissingInvalidDuplicateOrUnexpectedSettings() {
        for(String text:new String[]{"", "# only comment\n", "sync.key.hex=", "sync.key.hex=1234",
                "sync.key.hex="+HEX+"00", "sync.key.hex="+HEX.substring(1),
                "sync.key.hex="+HEX.substring(2)+"GG", "wrong="+HEX,
                "sync.key.hex="+HEX+"\nsync.key.hex="+HEX,
                "sync.key.hex="+HEX+"\nother=1", "sync.key.hex="+HEX+";"}) {
            Exception error=assertThrows(IllegalArgumentException.class,()->BuiltinSyncKey.parseKey(new StringReader(text)));
            assertFalse(error.getMessage().contains(HEX),"Invalid configuration must not be printed");
        }
    }
    @Test void packagedConfigurationMatchesRootAndDeviceWhenPresent() throws Exception {
        Path server=Paths.get(System.getProperty("basedir"));
        byte[] rootKey;
        try(Reader reader=Files.newBufferedReader(server.resolve("vault-sync-key.properties"),StandardCharsets.UTF_8)) {
            rootKey=BuiltinSyncKey.parseKey(reader);
        }
        assertTrue(BuiltinSyncKey.proof(rootKey).equals(BuiltinSyncKey.proof()),"Packaged sync configuration is stale");
        Path device=server.resolve("../firmware/vault-sync-key.properties").normalize();
        if(Files.exists(device))try(Reader reader=Files.newBufferedReader(device,StandardCharsets.UTF_8)) {
            assertTrue(Arrays.equals(rootKey,BuiltinSyncKey.parseKey(reader)),"Device and server sync configurations differ");
        }
    }
}
