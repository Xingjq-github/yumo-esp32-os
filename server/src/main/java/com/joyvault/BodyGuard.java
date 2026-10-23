package com.joyvault;
import javax.servlet.*;
import javax.servlet.http.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.Semaphore;
import java.util.concurrent.atomic.AtomicBoolean;

/** Bounded nonblocking request read, absolute 10s deadline and same-origin writes. */
final class BodyGuard implements Filter {
    static final String BODY=BodyGuard.class.getName()+".body";
    private final String origin;private final Semaphore active=new Semaphore(16);
    BodyGuard(String origin){this.origin=origin;}
    private static final class Buffer extends ByteArrayOutputStream {void erase(){Arrays.fill(buf,(byte)0);reset();}}
    private final class Exchange {
        final AtomicBoolean readDone=new AtomicBoolean(),released=new AtomicBoolean();final Buffer bytes=new Buffer();byte[] body;
        final long deadline=System.nanoTime()+10_000_000_000L;
        void release(){if(released.compareAndSet(false,true)){if(body!=null)Arrays.fill(body,(byte)0);bytes.erase();active.release();}}
    }
    static void error(HttpServletResponse response,int code,String message)throws IOException{if(response.isCommitted())return;response.setStatus(code);response.setContentType("application/json;charset=UTF-8");response.setHeader("Connection","close");response.getOutputStream().write(Json.bytes(Json.map("detail",message)));}
    private static void headers(HttpServletResponse r){r.setHeader("Cache-Control","no-store");r.setHeader("X-Content-Type-Options","nosniff");r.setHeader("X-Frame-Options","DENY");r.setHeader("Referrer-Policy","no-referrer");r.setHeader("Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");r.setHeader("Permissions-Policy","camera=(), microphone=(), geolocation=()");}
    @Override public void doFilter(ServletRequest request,ServletResponse response,FilterChain chain)throws IOException,ServletException{
        HttpServletRequest r=(HttpServletRequest)request;HttpServletResponse s=(HttpServletResponse)response;
        if(r.getDispatcherType()==DispatcherType.ASYNC){chain.doFilter(r,s);return;}
        headers(s);boolean mutating=!Java8.set("GET","HEAD","OPTIONS").contains(r.getMethod());
        if(mutating&&!r.getRequestURI().startsWith("/api/device/")&&!origin.equals(r.getHeader("Origin"))){error(s,403,"请求来源不匹配");return;}
        if(!active.tryAcquire()){error(s,503,"服务繁忙，请稍后重试");return;}
        Exchange x=new Exchange();final int maximum;
        switch(r.getRequestURI()){
            case "/api/entries":maximum=32768;break;
            case "/api/entries/import":maximum=ImportService.MAX_IMPORT_BODY;break;
            case "/api/archive/import":maximum=ImportService.MAX_ARCHIVE_BODY;break;
            default:maximum=8192;
        }
        if(r.getContentLengthLong()>maximum){try{error(s,413,"请求过大");}finally{x.release();}return;}
        if(r.getContentLengthLong()==0||(r.getContentLengthLong()<0&&r.getHeader("Transfer-Encoding")==null)){x.body=new byte[0];r.setAttribute(BODY,x.body);try{chain.doFilter(r,s);}finally{x.release();}return;}
        AsyncContext context=r.startAsync();context.setTimeout(10000);
        context.addListener(new AsyncListener(){
            public void onComplete(AsyncEvent e){x.release();}
            public void onTimeout(AsyncEvent e)throws IOException{if(x.readDone.compareAndSet(false,true)){try{error(s,408,"请求超时");}finally{context.complete();}}}
            public void onError(AsyncEvent e){x.release();}
            public void onStartAsync(AsyncEvent e){}
        });
        ServletInputStream input=r.getInputStream();input.setReadListener(new ReadListener(){
            private void fail(int code,String message)throws IOException{if(x.readDone.compareAndSet(false,true)){try{error(s,code,message);}finally{context.complete();}}}
            public void onDataAvailable()throws IOException{byte[] part=new byte[4096];try{while(!x.readDone.get()&&input.isReady()&&!input.isFinished()){if(System.nanoTime()>x.deadline){fail(408,"请求超时");return;}int n=input.read(part);if(n<0)break;if(x.bytes.size()+n>maximum){fail(413,"请求过大");return;}x.bytes.write(part,0,n);}}finally{Arrays.fill(part,(byte)0);}}
            public void onAllDataRead()throws IOException{if(System.nanoTime()>x.deadline){fail(408,"请求超时");return;}if(x.readDone.compareAndSet(false,true)){x.body=x.bytes.toByteArray();x.bytes.erase();r.setAttribute(BODY,x.body);context.setTimeout(0);context.dispatch();}}
            public void onError(Throwable t){try{fail(400,"请求格式错误");}catch(IOException ignored){x.release();}}
        });
    }
}
