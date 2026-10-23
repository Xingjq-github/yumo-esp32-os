package com.joyvault;

/** Fixed, credential-free client errors. Never use user input as an exception message. */
public final class ApiException extends RuntimeException {
    public final int status;
    public ApiException(int status, String message) { super(message, null, false, false); this.status = status; }
    public static ApiException bad(String message) { return new ApiException(400, message); }
}
