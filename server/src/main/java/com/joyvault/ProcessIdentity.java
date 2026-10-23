package com.joyvault;

import java.lang.management.ManagementFactory;
import java.time.Instant;

/** HotSpot/Temurin Java 8 identity for the authenticated local stop channel. */
final class ProcessIdentity {
    private ProcessIdentity() {}
    static long pid() {
        String name=ManagementFactory.getRuntimeMXBean().getName();int separator=name.indexOf('@');
        if(separator<=0)throw new IllegalStateException("Local process identity unavailable");
        long pid=Long.parseLong(name.substring(0,separator));
        if(pid<=0)throw new IllegalStateException("Invalid local process identity");
        return pid;
    }
    static String started(){return Instant.ofEpochMilli(ManagementFactory.getRuntimeMXBean().getStartTime()).toString();}
}
