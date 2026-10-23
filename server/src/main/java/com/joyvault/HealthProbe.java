package com.joyvault;
import java.io.*;
import java.net.*;
import java.util.Map;
final class HealthProbe {
    static URI endpoint(Map<String,String> environment) {
        String origin=environment.getOrDefault("VAULT_HEALTH_URL",environment.getOrDefault("VAULT_PUBLIC_URL","http://127.0.0.1:8443"));
        return URI.create(Settings.normalize(origin)+"/healthz");
    }
    static boolean healthy(URI endpoint)throws IOException {
        HttpURLConnection connection=(HttpURLConnection)endpoint.toURL().openConnection(Proxy.NO_PROXY);
        long deadline=System.nanoTime()+4_000_000_000L;
        try {
            connection.setConnectTimeout(3000);connection.setReadTimeout(4000);
            connection.setInstanceFollowRedirects(false);connection.setUseCaches(false);
            connection.connect();connection.setReadTimeout(remaining(deadline));
            if(connection.getResponseCode()!=200)return false;
            try(InputStream input=connection.getInputStream()) {
                ByteArrayOutputStream bytes=new ByteArrayOutputStream();byte[] chunk=new byte[1025];
                for(;;){connection.setReadTimeout(remaining(deadline));int n=input.read(chunk);if(n<0)break;if(n>1024-bytes.size())return false;bytes.write(chunk,0,n);}
                return Json.object(bytes.toByteArray()).equals(Json.map("status","ok","protocol",2));
            }
        }finally{connection.disconnect();}
    }
    private static int remaining(long deadline)throws SocketTimeoutException {
        long nanos=deadline-System.nanoTime();if(nanos<=0)throw new SocketTimeoutException("Probe deadline");
        return (int)Math.max(1,(nanos+999999)/1000000);
    }
    static void run(){
        try{if(!healthy(endpoint(System.getenv())))throw new IOException();System.out.println("healthy");}
        catch(Exception e){System.out.println("unhealthy");System.exit(1);}
    }
}
