package com.joyvault;
import java.net.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.security.MessageDigest;
import java.io.*;
import java.util.*;
import javax.annotation.PreDestroy;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.event.EventListener;
import org.springframework.boot.context.event.ApplicationReadyEvent;

/** Optional authenticated loopback-only graceful stop; never kill a saved PID. */
final class ShutdownControl {
    private final ConfigurableApplicationContext context;private final Settings settings;
    private ServerSocket socket;private Path state;private String token;private volatile boolean closing;
    ShutdownControl(ConfigurableApplicationContext context,Settings settings){this.context=context;this.settings=settings;}
    @EventListener(ApplicationReadyEvent.class)public void ready()throws Exception{
        String file=System.getenv("VAULT_CONTROL_FILE");if(file==null||file.codePoints().allMatch(Character::isWhitespace))return;
        state=Paths.get(file).toAbsolutePath().normalize();Files.createDirectories(state.getParent());token=VaultProtocol.randomHex(32);
        socket=new ServerSocket();socket.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"),0),4);
        long pid=ProcessIdentity.pid();
        byte[] record=Json.bytes(Json.map("pid",pid,"started",ProcessIdentity.started(),"port",socket.getLocalPort(),"token",token,"url",settings.origin()));
        Path temporary=state.resolveSibling(state.getFileName()+".new-"+pid);Files.write(temporary,record,StandardOpenOption.CREATE_NEW);
        try{Files.move(temporary,state,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);}
        catch(AtomicMoveNotSupportedException e){Files.move(temporary,state,StandardCopyOption.REPLACE_EXISTING);}
        Thread worker=new Thread(this::serve,"vault-local-control");worker.setDaemon(true);worker.start();
    }
    private void serve(){
        while(!closing){
            try(Socket peer=socket.accept()){
                peer.setSoTimeout(2000);ByteArrayOutputStream input=new ByteArrayOutputStream();int c;
                while(input.size()<100&&(c=peer.getInputStream().read())!=-1&&c!='\n')input.write(c);
                String command=new String(input.toByteArray(),StandardCharsets.US_ASCII).trim();
                boolean stop=MessageDigest.isEqual(command.getBytes(StandardCharsets.US_ASCII),("STOP "+token).getBytes(StandardCharsets.US_ASCII));
                if(stop){peer.getOutputStream().write("OK\n".getBytes(StandardCharsets.US_ASCII));peer.getOutputStream().flush();closing=true;}
                else peer.getOutputStream().write("DENIED\n".getBytes(StandardCharsets.US_ASCII));
            }catch(IOException ignored){}
            if(closing){context.close();System.exit(0);}
        }
    }
    @PreDestroy public void close(){
        closing=true;if(socket!=null)try{socket.close();}catch(IOException ignored){}
        if(state!=null)try{Map<String,Object> value=Json.object(Files.readAllBytes(state));if(Json.integerEquals(value.get("pid"),ProcessIdentity.pid())&&Objects.equals(value.get("token"),token))Files.deleteIfExists(state);}catch(Exception ignored){}
    }
}
