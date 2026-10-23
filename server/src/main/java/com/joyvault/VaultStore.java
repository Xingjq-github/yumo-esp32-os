package com.joyvault;

import java.nio.channels.*;
import java.nio.file.*;
import java.sql.*;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Supplier;
import javax.annotation.PreDestroy;

/** One small SQLite connection and one transaction boundary; no pool or plaintext cache. */
public final class VaultStore {
    final ReentrantLock lock=new ReentrantLock(true);
    final VaultProtocol protocol;
    private Connection db;
    private FileChannel lockChannel;
    private FileLock directoryLock;
    private boolean closed;
    static final class Snapshot {
        private final long revision;private final byte[] blob;
        Snapshot(long revision,byte[] blob){this.revision=revision;this.blob=blob;}
        long revision(){return revision;}byte[] blob(){return blob;}
    }
    static final class Content {
        private final long revision;private final List<Map<String,Object>> entries;
        Content(long revision,List<Map<String,Object>> entries){this.revision=revision;this.entries=entries;}
        long revision(){return revision;}List<Map<String,Object>> entries(){return entries;}
    }
    static final class Admin {
        private final byte[] salt,hash;private final String algorithm;
        Admin(byte[] salt,byte[] hash,String algorithm){this.salt=salt;this.hash=hash;this.algorithm=algorithm;}
        byte[] salt(){return salt;}byte[] hash(){return hash;}String algorithm(){return algorithm;}
    }
    VaultStore(Settings settings,DeploymentKeys keys) {
        protocol=new VaultProtocol(keys);
        try {
            Files.createDirectories(settings.data());
            lockChannel=FileChannel.open(settings.data().resolve("service.lock"),StandardOpenOption.CREATE,StandardOpenOption.WRITE);
            directoryLock=lockChannel.tryLock();
            if(directoryLock==null) throw new IllegalStateException("Data directory already in use");
            db=DriverManager.getConnection("jdbc:sqlite:"+settings.data().resolve("vault.db"));
            try(Statement s=db.createStatement()) {
                s.execute("PRAGMA busy_timeout=5000"); s.execute("PRAGMA synchronous=FULL");
                s.execute("PRAGMA secure_delete=ON");s.execute("PRAGMA journal_mode=DELETE");
                s.execute("CREATE TABLE IF NOT EXISTS admin(id INTEGER PRIMARY KEY CHECK(id=1),salt BLOB NOT NULL,hash BLOB NOT NULL,algorithm TEXT NOT NULL DEFAULT 'pbkdf2')");
                s.execute("CREATE TABLE IF NOT EXISTS vault(id INTEGER PRIMARY KEY CHECK(id=1),revision INTEGER NOT NULL,blob BLOB NOT NULL)");
            }
            Snapshot row=current();
            if(row!=null)protocol.decode(row.blob());
        } catch(Exception e) {
            close();throw new IllegalStateException("Cannot open vault: check directory permissions, existing keys and database format; no data was reset");
        }
    }
    <T>T transaction(Supplier<T> operation) {
        lock.lock();
        try {
            db.setAutoCommit(false);
            try {
                T value = operation.get();
                db.commit();
                return value;
            } catch (SQLException | RuntimeException | Error failure) {
                // A failed commit also needs rollback. Restoring autoCommit first
                // could otherwise commit work from an operation reported as failed.
                try { db.rollback(); }
                catch (SQLException rollbackFailure) {
                    failure.addSuppressed(rollbackFailure);
                    close(); // Never reuse a connection with an uncertain transaction.
                }
                throw failure;
            } finally {
                if (!closed) {
                    try { db.setAutoCommit(true); }
                    catch (SQLException restoreFailure) { close(); throw restoreFailure; }
                }
            }
        } catch(SQLException e) { throw new IllegalStateException("Database transaction failed"); }
        finally { lock.unlock(); }
    }
    boolean healthy() {
        try {
            if(!lock.tryLock(1,TimeUnit.SECONDS))return false;
            try {if(closed || db==null)return false;try(Statement s=db.createStatement();ResultSet r=s.executeQuery("SELECT 1")){return r.next()&&r.getInt(1)==1;}}
            finally{lock.unlock();}
        } catch(Exception e){return false;}
    }
    Admin admin() {
        lock.lock();
        try(Statement s=db.createStatement();ResultSet r=s.executeQuery("SELECT salt,hash,algorithm FROM admin WHERE id=1")) {
            return r.next()?new Admin(r.getBytes(1),r.getBytes(2),r.getString(3)):null;
        } catch(SQLException e){throw new IllegalStateException("Administrator record read failed");}
        finally{lock.unlock();}
    }
    boolean initialized(){return admin()!=null;}
    void writeAdmin(byte[] salt,byte[] hash,boolean replace) {
        if(!lock.isHeldByCurrentThread())throw new IllegalStateException("Transaction required");
        String sql=replace?"UPDATE admin SET salt=?,hash=?,algorithm='pbkdf2' WHERE id=1":"INSERT INTO admin(salt,hash,algorithm,id) VALUES(?,?,'pbkdf2',1)";
        try(PreparedStatement s=db.prepareStatement(sql)){s.setBytes(1,salt);s.setBytes(2,hash);s.executeUpdate();}
        catch(SQLException e){throw new IllegalStateException("Administrator record write failed");}
    }
    Snapshot current() {
        lock.lock();
        try(Statement s=db.createStatement();ResultSet r=s.executeQuery("SELECT revision,blob FROM vault WHERE id=1")) {
            return r.next()?new Snapshot(r.getLong(1),r.getBytes(2)):null;
        }catch(SQLException e){throw new IllegalStateException("Vault read failed");}
        finally{lock.unlock();}
    }
    Content read(){Snapshot row=current();return row==null?new Content(0,new ArrayList<>()):new Content(row.revision(),protocol.decode(row.blob()));}
    long save(List<Map<String,Object>> entries,long revision) {
        if(!lock.isHeldByCurrentThread())throw new IllegalStateException("Transaction required");
        long next=revision+1;
        if(next>0xffffffffL)throw new ApiException(409,"版本已达上限");
        byte[] blob=protocol.encode(entries,next);
        try(PreparedStatement s=db.prepareStatement("INSERT OR REPLACE INTO vault(id,revision,blob) VALUES(1,?,?)")) {
            s.setLong(1,next);s.setBytes(2,blob);s.executeUpdate();return next;
        }catch(SQLException e){throw new IllegalStateException("Vault write failed");}
    }
    @PreDestroy public void close() {
        lock.lock();
        try {
            if(closed)return;closed=true;
            if(db!=null)try{db.close();}catch(SQLException ignored){}
            if(directoryLock!=null)try{directoryLock.release();}catch(Exception ignored){}
            if(lockChannel!=null)try{lockChannel.close();}catch(Exception ignored){}
        }finally{lock.unlock();}
    }
}
