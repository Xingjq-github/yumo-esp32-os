package com.joyvault;
import java.net.URI;
import java.util.*;
import javax.servlet.DispatcherType;
import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.boot.web.servlet.FilterRegistrationBean;
import org.springframework.context.ConfigurableApplicationContext;
import org.springframework.context.annotation.Bean;
import org.springframework.core.Ordered;
import org.springframework.core.env.Environment;

@SpringBootApplication
public class VaultApplication {
    public static void main(String[] args){
        if(args.length==1&&args[0].equals("healthcheck")){HealthProbe.run();return;}
        String origin=Settings.normalize(System.getenv().getOrDefault("VAULT_PUBLIC_URL","http://127.0.0.1:8443"));
        int port=URI.create(origin).getPort();if(port<0)port=origin.startsWith("https:")?443:80;
        SpringApplication app=new SpringApplication(VaultApplication.class);
        app.setDefaultProperties(Json.map("server.port",Integer.toString(port),"server.address",System.getenv().getOrDefault("VAULT_BIND_IP","0.0.0.0"),"vault.public-url",origin));app.run(args);
    }
    @Bean Settings settings(Environment env){return Settings.from(env);}
    @Bean DeploymentKeys keys(Settings settings){return DeploymentKeys.load(settings.keys());}
    @Bean VaultStore store(Settings settings,DeploymentKeys keys){return new VaultStore(settings,keys);}
    @Bean AuthService auth(VaultStore store,Settings settings){return new AuthService(store,settings);}
    @Bean FilterRegistrationBean<BodyGuard> bodyGuard(Settings settings){FilterRegistrationBean<BodyGuard> bean=new FilterRegistrationBean<>(new BodyGuard(settings.origin()));bean.setOrder(Ordered.HIGHEST_PRECEDENCE);bean.setAsyncSupported(true);bean.setDispatcherTypes(DispatcherType.REQUEST,DispatcherType.ASYNC);return bean;}
    @Bean ShutdownControl shutdownControl(ConfigurableApplicationContext context,Settings settings){return new ShutdownControl(context,settings);}
}
