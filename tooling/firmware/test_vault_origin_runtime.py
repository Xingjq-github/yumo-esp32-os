"""Compile and exercise the production origin parser, SD parser and encrypted config codec.

Only synthetic keys/data are used. The firmware RNG, UTF-8 service and task
yield are host stubs; real production cJSON and AES-GCM perform the codec.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, subprocess

ROOT = Path(__file__).resolve().parents[2]

def read(name):
    return (ROOT/'firmware/main/core'/name).read_text(encoding='utf8')

def section(source, start, end):
    offset = source.index(start)
    return source[offset:source.index(end, offset + len(start))]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path,
                        help='New run directory under artifacts/validation.')
    parser.add_argument('--compiler', type=Path, required=True, help='External host g++ compiler path')
    parser.add_argument('--cjson-root', type=Path, required=True, help='External directory containing cJSON.c and cJSON.h')
    args = parser.parse_args()
    run = args.output.resolve()
    if not run.is_relative_to(ROOT/'artifacts/validation'):
        parser.error('--output must be inside the project artifacts/validation directory')
    run.mkdir(parents=True, exist_ok=False)
    model = section(read('password_vault.hpp'), 'struct Entry {', 'enum class Operation')
    codec = re.sub(r'^#include .*\n', '', read('vault_json.cpp'), flags=re.M)
    declarations = re.sub(r'^#(?:include|pragma).*\n', '', read('vault_json.hpp'), flags=re.M)
    gcm = section(read('vault_crypto.cpp'), 'bool gcm(', 'bool encryptEmpty(')
    sd = section(read('vault_connection.cpp'), 'bool ConnectionConfig::parse(', 'bool ConnectionConfig::readSd(')
    source = r"""
#include "core/vault_crypto.hpp"
#include "core/vault_gcm.h"
#include "core/vault_origin.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#define ESP_LOGW(...) ((void)0)
void vTaskDelay(int) {}
void esp_fill_random(void *p,size_t size) {
  static unsigned value=0;auto out=static_cast<unsigned char *>(p);
  while(size--)*out++=static_cast<unsigned char>(++value);
}
namespace core {bool isValidUtf8(const char *,size_t){return true;}}
namespace vault {
""" + model + gcm + '\n}\n' + declarations + '\n' + codec + r"""
namespace vault {
struct ConnectionConfig {
  std::string url;
  static bool parse(const std::string &, ConnectionConfig &, std::string &);
};
""" + sd + r"""
}
static unsigned checks=0, origins=0, configs=0, sdconfigs=0;
static void check(bool ok,int line) {
  ++checks;
  if(!ok){std::fprintf(stderr,"failed check at line %d\n",line);std::exit(1);}
}
#define CHECK(v) check((v),__LINE__)
vault::Session synthetic() {
  vault::Session s;s.key.bytes.fill(0x39);s.id.fill(0x23);s.salt.fill(0x72);
  s.url="http://example.test:8443";s.pinSalt=std::string(32,'a');s.pinHash=std::string(64,'b');
  s.patternAuth=true;return s;
}
std::string jsonUrl(const std::string &url) {
  cJSON *s=cJSON_CreateString(url.c_str());CHECK(s);
  char *encoded=cJSON_PrintUnformatted(s);CHECK(encoded);
  std::string result(encoded);cJSON_free(encoded);cJSON_Delete(s);return result;
}
vault::Bytes envelope(const std::string &json,const vault::Session &s) {
  uint8_t aad[41],nonce[12]{};std::memcpy(aad,"JYCONFIG2",9);
  std::memcpy(aad+9,s.id.data(),16);std::memcpy(aad+25,s.salt.data(),16);
  vault::Bytes cipher;CHECK(vault::gcm(true,s.key,nonce,aad,41,
    reinterpret_cast<const uint8_t *>(json.data()),json.size(),cipher));
  vault::Bytes result(nonce,nonce+12);result.insert(result.end(),cipher.begin(),cipher.end());return result;
}
std::string config(const std::string &url) {
  const auto base=synthetic();
  return "{\"schema\":2,\"auth\":\"pattern-v1\",\"url\":"+jsonUrl(url)+
    ",\"pin_salt\":\""+base.pinSalt+"\",\"pin_hash\":\""+base.pinHash+"\"}";
}
void good(const std::string &input,const std::string &expected) {
  ++origins;
  std::string out="keep";CHECK(vault::normalizeOrigin(input,out));CHECK(out==expected);
  CHECK(vault::validOrigin(input));CHECK(out.back()!='/');
  std::string again="unchanged";CHECK(vault::normalizeOrigin(out,again));CHECK(again==out);
  std::string alias=input;CHECK(vault::normalizeOrigin(alias,alias));CHECK(alias==expected);
  auto session=synthetic();const auto base=synthetic();
  CHECK(vault::configDecode(envelope(config(input),base),session));++configs;
  CHECK(session.url==expected && session.pinSalt==base.pinSalt && session.pinHash==base.pinHash && session.patternAuth);
  vault::Bytes encoded;CHECK(vault::configEncode(session,encoded));
  auto restored=synthetic();CHECK(vault::configDecode(encoded,restored));CHECK(restored.url==expected);
  vault::ConnectionConfig sd;sd.url="keep";std::string error;
  CHECK(vault::ConnectionConfig::parse("{\"schema\":1,\"url\":"+jsonUrl(input)+"}",sd,error));++sdconfigs;
  CHECK(sd.url==expected);
}
void bad(const std::string &input,bool json=true) {
  ++origins;std::string out="keep";CHECK(!vault::normalizeOrigin(input,out));CHECK(out=="keep");
  CHECK(!vault::validOrigin(input));std::string alias=input;
  CHECK(!vault::normalizeOrigin(alias,alias));CHECK(alias==input);
  if(!json)return; // Embedded NUL cannot round-trip through cJSON string creation.
  auto session=synthetic();const auto base=synthetic();
  CHECK(!vault::configDecode(envelope(config(input),base),session));++configs;
  CHECK(session.url==base.url && session.pinSalt==base.pinSalt && session.pinHash==base.pinHash);
  vault::ConnectionConfig sd;sd.url="keep";std::string error;
  CHECK(!vault::ConnectionConfig::parse("{\"schema\":1,\"url\":"+jsonUrl(input)+"}",sd,error));++sdconfigs;
  CHECK(sd.url=="keep" && !error.empty());
}
int main() {
  good("https://vault.example.com/","https://vault.example.com");
  good("HTTPS://VAULT.EXAMPLE.COM/","https://vault.example.com");
  good(" \t\r\nHTTPS://VAULT.EXAMPLE.COM/\f\v ","https://vault.example.com");
  good("https://vault.example.com:443/","https://vault.example.com:443");
  good("https://vault.example.com:00443","https://vault.example.com:443");
  good("http://192.168.1.8:9080","http://192.168.1.8:9080");
  good("http://192.168.1.8/","http://192.168.1.8");
  good("https://192.168.1.8","https://192.168.1.8");
  good("http://localhost:80","http://localhost:80");
  good("http://YUMO-NAS.local:65535/","http://yumo-nas.local:65535");
  good("http://example.test:1","http://example.test:1");
  good("https://xn--fiqs8s.test","https://xn--fiqs8s.test");
  good("https://a-b.example","https://a-b.example");
  good("http://0.0.0.0:8080","http://0.0.0.0:8080");
  good("http://255.255.255.255:8080","http://255.255.255.255:8080");
  for(const char *s:{"", " ", "https://", "https:///", "http:///host",
      "vault.example.com", "//vault.example.com", "ftp://host", "https:host",
      "https://host/path", "https://host//", "https://host/?x=1", "https://host?x=1",
      "https://host#", "https://user@host", "https://user:pass@host",
      "https://host:0", "https://host:65536", "https://host:99999", "https://host:",
      "https://host:+443", "https://host:-443", "https://host:4a3", "https://host:443:80",
      "https://host:000443", "https://host:443/path", "https://host.:443",
      "https://.host", "https://a..b", "https://-host", "https://host-",
      "https://a.-b", "https://a-.b", "https://host_name", "https://ho st",
      "https://[::1]:443", "https://::1", "https://host%2f", "https://host\\bad",
      "http://256.1.1.1", "http://1.2.3", "http://1.2.3.4.5", "http://12345",
      "http://127.1", "http://192.168.001.1", "https://host/./",
      "https://host/../", "https://host:443?x", "https://host:443#x"})
    bad(s);
  good("https://"+std::string(63,'a')+".test","https://"+std::string(63,'a')+".test");
  bad("https://"+std::string(64,'a')+".test");
  const std::string maximal="https://"+std::string(63,'a')+"."+std::string(63,'b')+
    "."+std::string(63,'c')+"."+std::string(40,'d');
  CHECK(maximal.size()==240);good(maximal,maximal);bad(maximal+"e");
  good(" \t"+maximal+"\r\n",maximal);
  for(unsigned c=0;c<=32;++c) {
    std::string s="https://ho";s+=char(c);s+="st";bad(s,c!=0);
  }
  for(unsigned c=127;c<=255;++c) {
    std::string s="https://ho";s+=char(c);s+="st";bad(s);
  }
  std::string sentinel="unchanged";
  CHECK(!vault::normalizeOrigin(std::string(241,' '),sentinel));CHECK(sentinel=="unchanged");
  for(const char *s:{"{}", "{\"schema\":1}", "{\"url\":\"https://host\"}",
       "{\"schema\":2,\"url\":\"https://host\"}", "{\"schema\":1,\"url\":null}",
       "{\"schema\":1,\"url\":\"https://host\",\"url\":\"http://host\"}",
       "{\"schema\":1,\"url\":\"https://host\",\"token\":\"synthetic\"}",
       "{\"schema\":1,\"url\":\"https://host\\u0000.evil\"}"}) {
    vault::ConnectionConfig sd;sd.url="keep";std::string error;
    CHECK(!vault::ConnectionConfig::parse(s,sd,error));++sdconfigs;CHECK(sd.url=="keep");
  }
  vault::ConnectionConfig bom;std::string error;
  CHECK(vault::ConnectionConfig::parse("\xef\xbb\xbf{\"schema\":1,\"url\":\"HTTPS://HOST/\"}",bom,error));
  CHECK(bom.url=="https://host");++sdconfigs;
  std::printf("{\"passed\":true,\"checks\":%u,\"origin_cases\":%u,\"encrypted_config_cases\":%u,\"sd_config_cases\":%u,\"real_aes_gcm\":true,\"synthetic_data_only\":true}\n",checks,origins,configs,sdconfigs);
}
"""
    cpp,exe=run/'origin-replay.cpp',run/('origin-replay.exe' if os.name=='nt' else 'origin-replay')
    cpp.write_text(source,encoding='utf8')
    compiler=args.compiler.resolve();cjson=args.cjson_root.resolve()
    if not compiler.is_file() or not (cjson/'cJSON.c').is_file() or not (cjson/'cJSON.h').is_file():
        parser.error('Provide an existing --compiler and --cjson-root')
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    command=[str(compiler),'-std=c++17','-O2','-Wall','-Wextra','-I'+str(ROOT/'firmware/main'),
        '-I'+str(cjson),
        str(cpp),str(ROOT/'firmware/main/core/vault_gcm.c'),
        str(cjson/'cJSON.c'),'-o',str(exe)]
    result=subprocess.run(command,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'compile.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:
        print(result.stderr.decode(errors='replace'));raise RuntimeError('compile failed')
    result=subprocess.run([str(exe)],env=env,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'replay.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:
        print(result.stderr.decode(errors='replace'));raise RuntimeError('replay failed')
    summary=json.loads(result.stdout)
    worker=read('password_vault.cpp')
    save=section(worker, '    if (job.operation == Operation::SaveConfig)', '    } else {\n      // Validate')
    assert 'normalizeOrigin(job.url, origin)' in save and 'result->url = origin;' in save
    assert 'validOrigin(job.url)' not in save and 'job.url.rfind' not in save
    ui=(ROOT/'firmware/main/apps/password_vault.cpp').read_text(encoding='utf8')
    assert 'https://example.com' in ui and '局域网服务地址' not in ui
    summary.update(method='Compile actual origin header, unmodified codec/SD parse bodies, production cJSON and AES-GCM; synthetic credentials only.',
      limits='No device, network, TLS, SD I/O or actual SaveConfig storage writes. SaveConfig and UI integration checked as source contracts.',
      saveconfig_source_contract=True,ui_source_contract=True,
      sources={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
         for p in [ROOT/'firmware/main/core'/n for n in ['vault_origin.hpp','vault_json.hpp','vault_json.cpp',
            'vault_connection.cpp','password_vault.cpp']]+[ROOT/'firmware/main/apps/password_vault.cpp']})
    (run/'result.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
    print(json.dumps(summary))

if __name__=='__main__':
    main()
