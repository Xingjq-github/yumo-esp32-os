package com.joyvault;
import java.util.*;
import javax.servlet.http.HttpServletRequest;
import java.util.stream.Collectors;
import org.springframework.core.io.ClassPathResource;
import org.springframework.http.*;
import org.springframework.web.bind.annotation.*;

@RestController
final class VaultController {
    private final VaultStore store;private final AuthService auth;private final Settings settings;
    VaultController(VaultStore store,AuthService auth,Settings settings){this.store=store;this.auth=auth;this.settings=settings;}
    private static Map<String,Object> body(HttpServletRequest request){return Json.object((byte[])request.getAttribute(BodyGuard.BODY));}
    @GetMapping({"/","/app.js","/style.css"}) ResponseEntity<ClassPathResource> asset(HttpServletRequest request){
        String path=request.getRequestURI();String name=path.equals("/")?"index.html":path.substring(1);
        String type=path.equals("/")?"text/html;charset=UTF-8":path.equals("/app.js")?"text/javascript;charset=UTF-8":"text/css;charset=UTF-8";
        return ResponseEntity.ok().header(HttpHeaders.CONTENT_TYPE,type).body(new ClassPathResource("static/"+name));
    }
    @GetMapping("/healthz")ResponseEntity<Map<String,Object>> health(){boolean ok=store.healthy();return ResponseEntity.status(ok?200:503).body(Json.map("status",ok?"ok":"unavailable","protocol",2));}
    @GetMapping("/api/status")Map<String,Object> status(){return Json.map("initialized",store.initialized(),"public_url",settings.origin(),"protocol",2);}
    @PostMapping("/api/setup")ResponseEntity<?> setup(HttpServletRequest r){return auth.setup(body(r));}
    @PostMapping("/api/login")ResponseEntity<?> login(HttpServletRequest r){return auth.login(r,body(r));}
    @PostMapping("/api/logout")ResponseEntity<?> logout(HttpServletRequest r){return auth.logout(r);}
    @PostMapping("/api/admin/password")ResponseEntity<?> password(HttpServletRequest r){auth.require(r);return auth.change(r,body(r));}
    @GetMapping("/api/super-mode")Map<String,Object> superMode(HttpServletRequest r){return Json.map("super_mode",auth.superMode(r));}
    @PostMapping("/api/super-mode")Map<String,Object> superModeChange(HttpServletRequest r){return auth.superMode(r,body(r));}
    @GetMapping("/api/entries")Map<String,Object> entries(HttpServletRequest r){
        store.lock.lock();try{boolean plaintext=auth.superMode(r);VaultStore.Content state=store.read();
            return Json.map("revision",state.revision(),"entries",state.entries().stream().map(e->Entries.visible(e,plaintext)).collect(Collectors.toList()),"categories",Entries.CATEGORIES,"max_entries",VaultProtocol.MAX_ENTRIES,"super_mode",plaintext);
        }finally{store.lock.unlock();}
    }
    @PostMapping("/api/entries")Map<String,Object> save(HttpServletRequest r){
        auth.require(r);Map<String,Object> body=body(r);
        return store.transaction(()->{
            auth.require(r);VaultStore.Content state=store.read();revision(body,state.revision(),false);List<Map<String,Object>> items=state.entries();
            Map<String,Object> old=items.stream().filter(e->Objects.equals(e.get("id"),body.get("id"))).findFirst().orElse(null);
            if(body.get("id")!=null&&!body.get("id").equals("")&&old==null)throw new ApiException(404,"条目不存在");
            if(old==null&&items.size()>=VaultProtocol.MAX_ENTRIES)throw new ApiException(409,"最多"+VaultProtocol.MAX_ENTRIES+"条密码");
            Map<String,Object> entry=Entries.validate(body,old);if(old==null)items.add(entry);else items.set(items.indexOf(old),entry);
            boolean plaintext=auth.superMode(r);
            return Json.map("revision",store.save(items,state.revision()),"entry",Entries.visible(entry,plaintext),"super_mode",plaintext);
        });
    }
    static void revision(Map<String,Object> body,long revision,boolean importing){if(!Json.integerEquals(body.get("revision"),revision))throw new ApiException(409,importing?"内容已更新，请重新预览后导入":"内容已更新，请刷新后重试");}
    private static boolean preview(Map<String,Object> body){if(!(body.get("preview")instanceof Boolean))throw ApiException.bad("请选择预览或确认导入");return (Boolean)body.get("preview");}
    @PostMapping("/api/entries/import")Map<String,Object> csv(HttpServletRequest r){
        auth.require(r);Map<String,Object> body=body(r);boolean preview=preview(body);
        return store.transaction(()->{auth.require(r);VaultStore.Content state=store.read();if(!preview)revision(body,state.revision(),true);
            ImportService.Plan plan=ImportService.csv(body.get("csv"),body.get("category"),state.entries());Map<String,Object> report=plan.report();long rev=state.revision();
            if(!preview){if(!Boolean.TRUE.equals(report.get("can_import")))throw ApiException.bad("导入检查未通过，请先预览并处理错误；没有写入任何条目");if(changed(report))rev=store.save(plan.entries(),rev);}
            report.put("revision",rev);report.put("committed",!preview);return plan.report(auth.superMode(r));});
    }
    private static boolean changed(Map<String,Object> report){return ((Number)report.get("added")).intValue()>0||((Number)report.get("updated")).intValue()>0||Boolean.TRUE.equals(report.get("reordered"));}
    @PostMapping("/api/archive/export")ResponseEntity<byte[]> export(HttpServletRequest r){store.lock.lock();try{auth.require(r);VaultStore.Snapshot row=store.current();long rev=row==null?0:row.revision();byte[] blob=row==null?store.protocol.encode(Collections.emptyList(),0):row.blob();return ResponseEntity.ok().contentType(MediaType.APPLICATION_OCTET_STREAM).header(HttpHeaders.CONTENT_DISPOSITION,"attachment; filename=\"joyvault-r"+rev+".jyvault\"").body(blob);}finally{store.lock.unlock();}}
    @PostMapping("/api/archive/import")Map<String,Object> archive(HttpServletRequest r){
        auth.require(r);Map<String,Object> body=body(r);boolean preview=preview(body);
        try{return store.transaction(()->{auth.require(r);VaultStore.Content state=store.read();if(!preview)revision(body,state.revision(),true);
            ImportService.Archive source=ImportService.readArchive(body.get("file"),store.protocol);ImportService.Plan plan=ImportService.archive(source.entries(),state.entries());long rev=state.revision();
            if(!preview&&changed(plan.report()))rev=store.save(plan.entries(),rev);
            plan.report().putAll(Json.map("revision",rev,"source_revision",source.revision(),"committed",!preview));return plan.report(auth.superMode(r));});}finally{body.clear();}
    }
    @PostMapping("/api/entries/{id}/delete")Map<String,Object> delete(@PathVariable String id,HttpServletRequest r){
        auth.require(r);Map<String,Object> body=body(r);return store.transaction(()->{auth.require(r);VaultStore.Content state=store.read();revision(body,state.revision(),false);
            List<Map<String,Object>> remaining=new ArrayList<>(state.entries().stream().filter(e->!e.get("id").equals(id)).collect(Collectors.toList()));
            if(remaining.size()==state.entries().size())throw new ApiException(404,"条目不存在");return Json.map("revision",store.save(remaining,state.revision()));});
    }
    @PostMapping("/api/entries/{id}/reveal")Map<String,Object> reveal(@PathVariable String id,HttpServletRequest r){auth.require(r);if(!auth.superMode(r))auth.verify(r,body(r).get("password"),null);auth.require(r);Map<String,Object> entry=store.read().entries().stream().filter(e->e.get("id").equals(id)).findFirst().orElseThrow(()->new ApiException(404,"条目不存在"));return Json.map("password",entry.get("password"));}
    private VaultStore.Snapshot deviceRow(HttpServletRequest r){auth.device(r);VaultStore.Snapshot row=store.current();if(row==null)throw new ApiException(404,"请先在网页完成初始化或登录");return row;}
    @GetMapping("/api/device/manifest")Map<String,Object> manifest(HttpServletRequest r){VaultStore.Snapshot row=deviceRow(r);Map<String,Object> info=VaultProtocol.inspect(row.blob());info.put("sha256",VaultProtocol.sha256(row.blob()));return info;}
    // Write directly: Spring ResponseEntity normalizes ETags and conditional requests.
    // The device contract requires the original unquoted revision, always a 200 bundle.
    @GetMapping("/api/device/bundle")void bundle(HttpServletRequest r,javax.servlet.http.HttpServletResponse response)throws java.io.IOException{
        VaultStore.Snapshot row=deviceRow(r);response.setStatus(200);response.setHeader("X-Vault-Key",BuiltinSyncKey.proof());response.setContentType("application/octet-stream");response.setHeader("ETag",Long.toString(row.revision()));response.setContentLength(row.blob().length);response.getOutputStream().write(row.blob());
    }
}
