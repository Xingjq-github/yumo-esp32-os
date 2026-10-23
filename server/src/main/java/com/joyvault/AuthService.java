package com.joyvault;

import java.security.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.locks.ReentrantLock;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import javax.servlet.http.*;
import org.springframework.http.*;

final class AuthService {
    private final VaultStore store;
    private final Settings settings;
    private final Map<String,Session> sessions=new LinkedHashMap<>();
    private static final class Session {
        final long until;boolean superMode;long modeVersion;
        Session(long until){this.until=until;}
    }
    private final Map<String,Failure> failures=new HashMap<>();
    private final ReentrantLock authentication=new ReentrantLock();
    private static final class Failure {
        private final long until;private final int count;
        Failure(long until,int count){this.until=until;this.count=count;}
        long until(){return until;}int count(){return count;}
    }
    AuthService(VaultStore store,Settings settings){this.store=store;this.settings=settings;}
    static byte[] passwordHash(String password,byte[] salt) {
        Json.utf8(password);PBEKeySpec spec=new PBEKeySpec(password.toCharArray(),salt,100000,256);
        try{return SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();}
        catch(GeneralSecurityException e){throw new IllegalStateException("Password hashing unavailable");}
        finally{spec.clearPassword();}
    }
    static String cookie(HttpServletRequest request) {
        Cookie[] cookies=request.getCookies();
        if(cookies!=null)for(Cookie c:cookies)if(c.getName().equals("jv"))return c.getValue();
        return "";
    }
    String require(HttpServletRequest request) {
        store.lock.lock();
        try{long now=System.nanoTime();sessions.values().removeIf(session->session.until<=now);
            String sid=cookie(request);if(!sessions.containsKey(sid))throw new ApiException(401,"请重新登录");return sid;}
        finally{store.lock.unlock();}
    }
    void device(HttpServletRequest request) {
        String supplied=Objects.toString(request.getHeader("Authorization"),"");
        if(!MessageDigest.isEqual(supplied.getBytes(StandardCharsets.UTF_8),("Bearer "+store.protocol.keys.token).getBytes(StandardCharsets.UTF_8)))
            throw new ApiException(401,"设备密钥不匹配");
        // Mandatory compile-time sync-key check. External settings cannot disable it.
        String proof=Objects.toString(request.getHeader("X-Vault-Key"),"");
        if(!MessageDigest.isEqual(proof.getBytes(StandardCharsets.US_ASCII),BuiltinSyncKey.proof().getBytes(StandardCharsets.US_ASCII)))
            throw new ApiException(403,"同步密钥不匹配");
    }
    private ResponseEntity<Map<String,Object>> session() {
        String sid=VaultProtocol.randomHex(32);
        store.lock.lock();
        try{long now=System.nanoTime();sessions.values().removeIf(session->session.until<=now);
            if(sessions.size()>=8)sessions.remove(sessions.keySet().iterator().next());
            sessions.put(sid,new Session(now+1800_000_000_000L));}
        finally{store.lock.unlock();}
        String cookie=ResponseCookie.from("jv",sid).httpOnly(true).secure(settings.origin().startsWith("https:"))
            .sameSite("strict").maxAge(1800).path("/").build().toString();
        return ResponseEntity.ok().header(HttpHeaders.SET_COOKIE,cookie).body(Json.map("ok",true));
    }
    private static String newPassword(Object value,Object confirmation,String message) {
        if(!(value instanceof String))throw ApiException.bad(message);
        String s=(String)value;
        if(Json.characters(s)<4 || Json.characters(s)>128 || !s.equals(confirmation))throw ApiException.bad(message);
        Json.utf8(s);return s;
    }
    ResponseEntity<Map<String,Object>> setup(Map<String,Object> body) {
        String pw=newPassword(body.get("password"),body.get("confirm"),"请输入两次相同的管理员密码，至少4位");
        if(!authentication.tryLock())throw new ApiException(429,"请稍后再试");
        try {
            if(store.initialized())throw new ApiException(409,"管理员已设置，请登录");
            byte[] salt=VaultProtocol.random(16),hash=passwordHash(pw,salt);
            store.transaction(()->{if(store.initialized())throw new ApiException(409,"管理员已设置");store.writeAdmin(salt,hash,false);store.save(new ArrayList<>(),0);return null;});
            return session();
        }finally{authentication.unlock();}
    }
    void verify(HttpServletRequest request,Object value,String replacement) {
        if(!(value instanceof String))throw ApiException.bad("密码格式错误");
        String pw=(String)value;
        if(Json.characters(pw)<1 || Json.characters(pw)>128)throw ApiException.bad("密码格式错误");
        if(!authentication.tryLock())throw new ApiException(429,"请稍后再试");
        try {
            String peer=request.getRemoteAddr();Failure f=failures.get(peer);
            if(f!=null && f.until()>System.nanoTime())throw new ApiException(429,"请稍后再试");
            if(replacement!=null)require(request);
            VaultStore.Admin admin=store.admin();
            if(admin!=null && !admin.algorithm().equals("pbkdf2"))throw new IllegalStateException("Legacy administrator migration is not enabled");
            boolean valid=admin!=null && MessageDigest.isEqual(passwordHash(pw,admin.salt()),admin.hash());
            if(!valid) {
                if(failures.size()>256)failures.clear();int count=Math.min((f==null?0:f.count())+1,6);
                failures.put(peer,new Failure(System.nanoTime()+Math.min(1<<count,30)*1_000_000_000L,count));
                throw new ApiException(401,"管理员密码不正确");
            }
            if(replacement!=null) {
                byte[] salt=VaultProtocol.random(16),hash=passwordHash(replacement,salt);
                store.lock.lock();
                try{store.transaction(()->{require(request);store.writeAdmin(salt,hash,true);return null;});sessions.clear();}
                finally{store.lock.unlock();}
            }
            failures.remove(peer);
        }finally{authentication.unlock();}
    }
    ResponseEntity<Map<String,Object>> login(HttpServletRequest request,Map<String,Object> body) {
        verify(request,body.get("password"),null);
        store.transaction(()->{if(store.current()==null)store.save(new ArrayList<>(),0);return null;});return session();
    }
    ResponseEntity<Map<String,Object>> change(HttpServletRequest request,Map<String,Object> body) {
        require(request);String next=newPassword(body.get("new_password"),body.get("confirm"),"请输入两次相同的新密码，至少4位、最多128位");
        verify(request,body.get("password"),next);return session();
    }
    boolean superMode(HttpServletRequest request) {
        store.lock.lock();try{return sessions.get(require(request)).superMode;}finally{store.lock.unlock();}
    }
    Map<String,Object> superMode(HttpServletRequest request,Map<String,Object> body) {
        if(!(body.get("enabled") instanceof Boolean))throw ApiException.bad("请选择开启或退出超管模式");
        boolean enabled=(Boolean)body.get("enabled");long version;
        store.lock.lock();
        try {
            Session session=sessions.get(require(request));version=session.modeVersion;
            if(!enabled){session.superMode=false;session.modeVersion++;return Json.map("super_mode",false);}
        }finally{store.lock.unlock();}
        // Verify once; no administrator password or plaintext vault is stored in a session.
        try{verify(request,body.get("password"),null);}finally{body.remove("password");}
        store.lock.lock();
        try {
            Session session=sessions.get(require(request));
            if(session.modeVersion!=version)throw new ApiException(409,"模式已改变，请重新开启");
            session.superMode=true;session.modeVersion++;
            return Json.map("super_mode",true);
        }finally{store.lock.unlock();}
    }
    ResponseEntity<Map<String,Object>> logout(HttpServletRequest request) {
        store.lock.lock();try{sessions.remove(cookie(request));}finally{store.lock.unlock();}
        return ResponseEntity.ok().header(HttpHeaders.SET_COOKIE,ResponseCookie.from("jv","").maxAge(0).path("/").sameSite("strict").build().toString()).body(Json.map("ok",true));
    }
}
