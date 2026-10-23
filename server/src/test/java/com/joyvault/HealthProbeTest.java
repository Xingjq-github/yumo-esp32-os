package com.joyvault;

import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import static org.junit.jupiter.api.Assertions.*;

class HealthProbeTest {
    interface Handler { void respond(com.sun.net.httpserver.HttpExchange exchange) throws IOException; }
    static final class Endpoint implements AutoCloseable {
        final HttpServer server;
        final ExecutorService executor=Executors.newSingleThreadExecutor();
        Endpoint(Handler handler)throws IOException {
            server=HttpServer.create(new InetSocketAddress("127.0.0.1",0),0);
            server.setExecutor(executor);
            server.createContext("/healthz",exchange->{try{handler.respond(exchange);}finally{exchange.close();}});
            server.start();
        }
        URI uri(){return URI.create("http://127.0.0.1:"+server.getAddress().getPort()+"/healthz");}
        public void close(){server.stop(0);executor.shutdownNow();}
    }
    boolean reply(int status,String text)throws IOException {
        try(Endpoint endpoint=new Endpoint(exchange->{
            byte[] bytes=text.getBytes(StandardCharsets.UTF_8);
            exchange.sendResponseHeaders(status,bytes.length);
            exchange.getResponseBody().write(bytes);
        })){return HealthProbe.healthy(endpoint.uri());}
    }
    @Test void acceptsExactHealthContract()throws Exception {assertTrue(reply(200,"{\"status\":\"ok\",\"protocol\":2}"));}
    @Test void rejectsWrongStatusAndProtocol()throws Exception {
        assertFalse(reply(503,"{\"status\":\"ok\",\"protocol\":2}"));
        assertFalse(reply(200,"{\"status\":\"ok\",\"protocol\":1}"));
        assertFalse(reply(200,"{\"status\":\"ok\",\"protocol\":2,\"extra\":true}"));
    }
    @Test void rejectsOversizeResponse()throws Exception {
        assertFalse(reply(200,String.join("",Collections.nCopies(1025,"x"))));
    }
    @Test void doesNotFollowRedirect()throws Exception {
        try(Endpoint endpoint=new Endpoint(exchange->{exchange.getResponseHeaders().set("Location","/healthz");exchange.sendResponseHeaders(302,-1);})){
            assertFalse(HealthProbe.healthy(endpoint.uri()));
        }
    }
    @Test void slowResponseHasOverallDeadline()throws Exception {
        try(Endpoint endpoint=new Endpoint(exchange->{
            exchange.sendResponseHeaders(200,0);
            try(OutputStream body=exchange.getResponseBody()) {
                for(int i=0;i<30;i++) {
                    body.write(' ');body.flush();
                    try{Thread.sleep(300);}catch(InterruptedException e){Thread.currentThread().interrupt();return;}
                }
            }
        })){
            long start=System.nanoTime();
            assertThrows(IOException.class,()->HealthProbe.healthy(endpoint.uri()));
            assertTrue(TimeUnit.NANOSECONDS.toMillis(System.nanoTime()-start)<5500,"Probe must not wait for a slowly streamed body");
        }
    }
}
