package com.joyvault;
import java.util.Map;
import javax.servlet.RequestDispatcher;
import javax.servlet.http.HttpServletRequest;
import org.springframework.boot.web.servlet.error.ErrorController;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.HttpRequestMethodNotSupportedException;
import org.springframework.web.servlet.NoHandlerFoundException;
@RestControllerAdvice
final class ApiErrors {
    @ExceptionHandler(ApiException.class)ResponseEntity<?> expected(ApiException e){return ResponseEntity.status(e.status).body(Json.map("detail",e.getMessage()));}
    @ExceptionHandler(HttpRequestMethodNotSupportedException.class)ResponseEntity<?> method(){return ResponseEntity.status(405).body(Json.map("detail","Method Not Allowed"));}
    @ExceptionHandler(NoHandlerFoundException.class)ResponseEntity<?> missing(){return ResponseEntity.status(404).body(Json.map("detail","Not Found"));}
    @ExceptionHandler(Exception.class)ResponseEntity<?> failure(){return ResponseEntity.status(500).body(Json.map("detail","服务内部错误"));}
}
@RestController
final class SafeErrorController implements ErrorController {
    @RequestMapping("/error")ResponseEntity<Map<String,Object>> error(HttpServletRequest request){Object raw=request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE);int status=raw instanceof Integer?(Integer)raw:500;return ResponseEntity.status(status).body(Json.map("detail",status==404?"Not Found":status==405?"Method Not Allowed":"请求处理失败"));}
}
