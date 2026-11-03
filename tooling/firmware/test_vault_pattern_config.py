"""Run production encrypted config codec with synthetic identities and credentials."""
from pathlib import Path
import argparse, json, os, re, subprocess

ROOT=Path(__file__).resolve().parents[2]

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--run-dir',required=True,type=Path)
    ap.add_argument('--compiler',required=True,type=Path,help='External host g++ compiler path')
    ap.add_argument('--cjson-root',required=True,type=Path,help='External directory containing cJSON.c and cJSON.h')
    a=ap.parse_args()
    run=a.run_dir.resolve();assert run.is_relative_to(ROOT/'artifacts/validation');run.mkdir(parents=True,exist_ok=False)
    def read(name):return (ROOT/'firmware/main/core'/name).read_text(encoding='utf-8')
    def section(source,start,end):
        offset=source.index(start);return source[offset:source.index(end,offset+len(start))]
    # Use the production structs/codec/GCM verbatim. Substitute only IDF RNG,
    # task yield and UTF-8 service; all generated test payloads are ASCII.
    model=section(read('password_vault.hpp'),'struct Entry {','enum class Operation')
    codec=re.sub(r'^#include .*\n','',read('vault_json.cpp'),flags=re.M)
    declarations=re.sub(r'^#(?:include|pragma).*\n','',read('vault_json.hpp'),flags=re.M)
    gcm=section(read('vault_crypto.cpp'),'bool gcm(', 'bool encryptEmpty(')
    source='''#include "core/vault_crypto.hpp"
#include "core/vault_gcm.h"
#include "core/vault_origin.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#define ESP_LOGW(...) ((void)0)
void vTaskDelay(int) {}
void esp_fill_random(void *p,size_t size) {static unsigned value=0;auto out=static_cast<unsigned char *>(p);while(size--)*out++=static_cast<unsigned char>(++value);}
namespace core {bool isValidUtf8(const char *,size_t){return true;}}
namespace vault {\n'''+model+gcm+'\n}\n'+declarations+'\n'+codec+r'''
vault::Session synthetic() {vault::Session s;s.key.bytes.fill(0x39);s.id.fill(0x23);s.salt.fill(0x72);s.url="http://example.test:8443";s.pinSalt=std::string(32,'a');s.pinHash=std::string(64,'b');return s;}
vault::Bytes envelope(const std::string &json,const vault::Session &s) {
  uint8_t aad[41],nonce[12]{};std::memcpy(aad,"JYCONFIG2",9);std::memcpy(aad+9,s.id.data(),16);std::memcpy(aad+25,s.salt.data(),16);
  vault::Bytes cipher;assert(vault::gcm(true,s.key,nonce,aad,41,reinterpret_cast<const uint8_t *>(json.data()),json.size(),cipher));
  vault::Bytes result(nonce,nonce+12);result.insert(result.end(),cipher.begin(),cipher.end());return result;
}
int main() {
  const auto base=synthetic();unsigned checks=0;
  const std::string tail="\"url\":\"http://example.test:8443\",\"pin_salt\":\""+base.pinSalt+"\",\"pin_hash\":\""+base.pinHash+"\"}";
  auto legacy=synthetic();assert(vault::configDecode(envelope("{\"schema\":2,"+tail,base),legacy));assert(!legacy.patternAuth);++checks;
  vault::Bytes out;assert(!vault::configEncode(legacy,out));++checks;
  auto current=synthetic();assert(vault::configDecode(envelope("{\"schema\":2,\"auth\":\"pattern-v1\","+tail,base),current));assert(current.patternAuth);++checks;
  vault::Bytes first,second;assert(vault::configEncode(current,first)&&vault::configEncode(current,second));assert(first!=second);++checks;
  auto roundtrip=synthetic();assert(vault::configDecode(first,roundtrip));assert(roundtrip.patternAuth&&roundtrip.url==base.url&&roundtrip.pinSalt==base.pinSalt&&roundtrip.pinHash==base.pinHash);++checks;
  for(const char *value:{"null","3","false","[]","{}","\"pattern-v2\"","\"pin\"","\"\""}) {
    auto reject=synthetic();assert(!vault::configDecode(envelope("{\"schema\":2,\"auth\":"+std::string(value)+","+tail,base),reject));++checks;
  }
  for(size_t i=0;i<first.size();++i) {auto corrupt=first;corrupt[i]^=1;auto reject=synthetic();assert(!vault::configDecode(corrupt,reject));assert(!reject.patternAuth);++checks;}
  auto wrong=synthetic();wrong.key.bytes[0]^=1;assert(!vault::configDecode(first,wrong));++checks;
  auto wrongId=synthetic();wrongId.id[0]^=1;assert(!vault::configDecode(first,wrongId));++checks;
  std::printf("{\"passed\":true,\"checks\":%u,\"real_aes_gcm\":true,\"synthetic_data_only\":true}\n",checks);
}
'''
    (run/'codec-replay.cpp').write_text(source,encoding='utf-8')
    compiler=a.compiler.resolve();cjson=a.cjson_root.resolve()
    if not compiler.is_file() or not (cjson/'cJSON.c').is_file() or not (cjson/'cJSON.h').is_file():
        ap.error('Provide an existing --compiler and --cjson-root')
    env=dict(os.environ);env['PATH']=str(compiler.parent)+os.pathsep+env.get('PATH','')
    command=[str(compiler),'-std=c++17','-O2','-I'+str(ROOT/'firmware/main'),'-I'+str(cjson),str(run/'codec-replay.cpp'),str(ROOT/'firmware/main/core/vault_gcm.c'),str(cjson/'cJSON.c'),'-o',str(run/'codec-replay.exe')]
    result=subprocess.run(command,env=env,capture_output=True,timeout=90,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'compile.log').write_bytes(result.stdout+result.stderr);assert result.returncode==0,run/'compile.log'
    result=subprocess.run([str(run/'codec-replay.exe')],env=env,capture_output=True,timeout=30,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
    (run/'replay.log').write_bytes(result.stdout+result.stderr);assert result.returncode==0,run/'replay.log'
    summary=json.loads(result.stdout);(run/'result.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps(summary))

if __name__=='__main__':main()
