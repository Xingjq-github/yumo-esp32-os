package com.joyvault;

import java.util.*;
/** Synthetic integration-test bridge only; not included in the executable JAR. */
public final class ProtocolFixtureCli {
    @SuppressWarnings("unchecked") public static void main(String[] args) throws Exception {
        Map<String,Object> input=Json.object(Java8.read(System.in,4*1024*1024));
        Map<String,Object> k=(Map<String,Object>)input.get("keys");
        DeploymentKeys keys=new DeploymentKeys(DeploymentKeys.hex(k,"key",32),(String)k.get("token"),DeploymentKeys.hex(k,"vault_id",16),DeploymentKeys.hex(k,"salt",16));
        VaultProtocol protocol=new VaultProtocol(keys);
        Object result;
        if ("sync-proof".equals(input.get("op"))) {
            result=BuiltinSyncKey.proof();
        } else if ("encode".equals(input.get("op"))) {
            result=Base64.getEncoder().encodeToString(protocol.encode((List<Map<String,Object>>)input.get("entries"),((Number)input.get("revision")).longValue()));
        } else if ("decode".equals(input.get("op"))) {
            result=protocol.decode(Base64.getDecoder().decode((String)input.get("blob")));
        } else { throw new IllegalArgumentException("Unknown fixture operation"); }
        System.out.write(Json.bytes(result));
    }
}
