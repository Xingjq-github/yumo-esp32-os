"""Version-independent manual sync contracts + synthetic state specification.
No compiler, device, HTTP endpoint, database or user password is accessed.
The model is not execution of the ESP32 C++ implementation.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import json,re,struct,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def read(name):return (ROOT/name).read_text(encoding='utf-8-sig')
def compact(text):return re.sub(r'\s+','',text)
def section(text,start,end):return text.split(start,1)[1].split(end,1)[0]

class RevisionIndependentSyncContracts(unittest.TestCase):
 def test_no_revision_gate_in_either_sync_attempt_or_noop(self):
  text=section(read('main/core/password_vault.cpp'),'// Validate the entire encrypted bundle','job.configWriteStarted = true;')
  self.assertNotIn('current.revision',text)
  self.assertNotIn('result->revision',text)
  self.assertNotIn('read32(bundle.data() + 16)',text)
  self.assertNotIn('版本回退',text)
  self.assertNotIn('同版本内容不同',text)
 def test_identity_and_framing_precede_authenticated_decode(self):
  s=compact(section(read('main/core/password_vault.cpp'),'auto attempt =','if (!attempt(ConnectionConfig{current.url}))'))
  for token in ('!framing(bundle.data(),bundle.size())','!equal(bundle.data()+24,current.id.data(),16)','!equal(bundle.data()+40,current.salt.data(),16)'):
   self.assertLess(s.index(token),s.index('result=decode(job,bundle,current.key)'))
  self.assertLess(s.index('if(!result)returnfalse'),s.index('copyConfig(current,*result)'))
 def test_noop_uses_exact_file_and_url_after_validation(self):
  s=compact(read('main/core/password_vault.cpp'))
  decision='if(bundle==disk.bundle&&result->url==current.url)'
  self.assertEqual(s.count(decision),1)
  self.assertLess(s.index('if(!attempt(fromSd))'),s.index(decision))
  tail=s.split(decision,1)[1]
  self.assertTrue(tail.startswith('{job.output=std::move(result);job.ok=!stopped(job);return;}'))
  self.assertIn('configEncode(*result,config)',tail)
  self.assertIn('commit(disk,bundle,config,job.error,job.cancelled)',tail)
 def test_revision_is_retained_as_metadata_not_copied_from_old_session(self):
  s=read('main/core/password_vault.cpp')
  self.assertIn('session->revision = read32(bundle.data() + 16)',s)
  copy=section(s,'void copyConfig(', 'void run(Job &job)')
  self.assertNotIn('revision',copy)
  for field in ('url','ca','token','pinSalt','pinHash'):self.assertIn('to.'+field+' = from.'+field,copy)
 def test_format_schema_and_size_limits_remain(self):
  s=compact(read('main/core/vault_format.hpp'))
  for token in ('MaxBundle=262144','MaxEntries=256','n>=144&&n<=MaxBundle','"JYVAULT2"','read32(p+20)==0'):
   self.assertIn(token,s)
  self.assertIn('if (!payload(plain.value, *session))',read('main/core/password_vault.cpp'))
 def test_authenticated_decryption_and_pin_preserved(self):
  s=read('main/core/password_vault.cpp')
  self.assertIn('!decryptBundle(bundle, key, plain.value)',s)
  self.assertIn('pattern::valid(job.master.data(), job.master.size())',s)
  self.assertIn('verifyPin(job, *result)',s)
  self.assertIn('builtin::Token, MaxBundle, bundle)',s)
 def test_local_slot_generation_is_independent_of_remote_revision(self):
  s=compact(read('main/core/vault_storage.cpp'))
  self.assertIn('b.generation>a.generation',s)
  self.assertIn('write32(h.data()+8,previous.generation+1)',s)
  self.assertNotIn('revision',s)
  self.assertLess(s.index('io.write(p,offset+Sector+i'),s.index('io.write(p,offset,h.data()'))
  self.assertIn('cancel.load()',s)
 def test_primary_and_sd_share_one_validator_and_bounded_retry(self):
  s=compact(read('main/core/password_vault.cpp'))
  for token in ('attempt(ConnectionConfig{current.url})','attempt(fromSd)','ConnectionConfig::readSd('):self.assertEqual(s.count(token),1)
  self.assertIn('if(!attempt(fromSd)){job.error="同步失败："+job.error;return;}',s)
  self.assertLess(s.index('if(!attempt(fromSd))'),s.index('job.sdConfigUsed=true'))
 def test_each_sync_click_launches_a_full_download(self):
  app=section(read('main/apps/password_vault.cpp'),'case Action::Sync: {','case Action::Network:')
  self.assertIn('std::make_shared<vault::Job>(vault::Operation::Sync)',app)
  self.assertIn('launch(job)',app)
  self.assertNotIn('revision',app)
  core=read('main/core/password_vault.cpp')
  self.assertLess(core.index('if (!attempt(ConnectionConfig{current.url}))'),core.index('if (bundle == disk.bundle'))
  self.assertNotIn('If-None-Match',read('main/core/vault_sync.cpp'))
 def test_snapshot_uses_fresh_entries_then_replaces_the_entire_ui_session(self):
  core=read('main/core/password_vault.cpp')
  decoder=compact(section(core,'std::shared_ptr<Session> decode(', 'bool enter('))
  self.assertIn('new(memory)Session',decoder)
  self.assertIn('payload(plain.value,*session)',decoder)
  copy=section(core,'void copyConfig(', 'void run(Job &job)')
  self.assertNotIn('entries',copy)
  self.assertNotIn('current.entries',core)
  app=compact(read('main/apps/password_vault.cpp'))
  self.assertIn('casevault::Operation::Sync:m_session=std::move(m_job->output);',app)
 def test_payload_accepts_an_empty_array_not_a_missing_or_invalid_array(self):
  payload=compact(section(read('main/core/vault_json.cpp'),'bool payload(', 'bool validOrigin('))
  self.assertIn('if(!cJSON_IsArray(entries)||cJSON_GetArraySize(entries)>int(MaxEntries))returnfalse;',payload)
  self.assertNotIn('cJSON_GetArraySize(entries)==0',payload)
  self.assertNotIn('cJSON_GetArraySize(entries)<',payload)
  self.assertNotIn('entries.empty()',payload)
  self.assertIn('cJSON_ArrayForEach(obj,entries)',payload)
  self.assertTrue(payload.endswith('returntrue;}'))
 def test_server_optimistic_edit_conflicts_are_not_relaxed(self):
  s=read('../server/src/main/java/com/joyvault/VaultController.java')
  self.assertIn('Json.integerEquals(body.get("revision"),revision)',s)
  self.assertIn('new ApiException(409,',s)
  self.assertIn('response.getOutputStream().write(row.blob())',s)

@dataclass(frozen=True)
class Bundle:
 revision:int
 content:bytes
 valid:bool=True
 identity:bool=True
 authenticated:bool=True
 payload:bool=True
 def wire(self):return struct.pack('<I',self.revision)+self.content
 def accepted(self):return self.valid and self.identity and self.authenticated and self.payload
@dataclass(frozen=True)
class State:
 bundle:Bundle
 url:str='http://old:8443'
 generation:int=9
 pin:str='unchanged-local-pin-verifier'

def accept(state,incoming,url,*,cancelled=False,commit_ok=True):
 if cancelled or not incoming.accepted():return state,'rejected'
 if incoming.wire()==state.bundle.wire() and url==state.url:return state,'unchanged'
 if not commit_ok:return state,'rejected'
 return replace(state,bundle=incoming,url=url,generation=state.generation+1),'committed'

def fallback(state,primary,secondary):
 for attempt,(bundle,url) in enumerate((primary,secondary),1):
  result,status=accept(state,bundle,url)
  if status!='rejected':return result,status,attempt
 return state,'rejected',2

class RevisionIndependentSyncModel(unittest.TestCase):
 def test_server_snapshot_replaces_all_fields_and_removes_deleted_entries(self):
  local={'schema':2,'entries':[
   {'id':'keep','title':'old','username':'old-user','password':'old-test','category':'work','target':'old','note':'old','favorite':False},
   {'id':'removed','title':'local-only','password':'synthetic'}]}
  remote={'schema':2,'entries':[
   {'id':'keep','title':'new','username':'new-user','password':'new-test','category':'web','target':'new','note':'new','favorite':True},
   {'id':'added','title':'server-only','password':'synthetic'}]}
  encode=lambda value:json.dumps(value,sort_keys=True).encode()
  old=State(Bundle(6,encode(local)))
  for revision in (3,6,8):
   incoming=Bundle(revision,encode(remote))
   state,status=accept(old,incoming,old.url)
   self.assertEqual(status,'committed')
   self.assertEqual(json.loads(state.bundle.content),remote)
   self.assertNotIn('removed',[entry['id'] for entry in json.loads(state.bundle.content)['entries']])
   self.assertEqual(state.pin,old.pin)
 def test_valid_empty_server_snapshot_clears_local_entries(self):
  old=State(Bundle(6,b'{"schema":2,"entries":[{"id":"local-only"}]}'))
  for revision in (0,3,6,8):
   incoming=Bundle(revision,b'{"schema":2,"entries":[]}')
   state,status=accept(old,incoming,old.url)
   self.assertEqual(status,'committed')
   self.assertEqual(json.loads(state.bundle.content)['entries'],[])
   self.assertEqual(state.bundle,incoming)
   self.assertEqual(state.pin,old.pin)
 def test_old_six_to_java_three_and_zero_are_saved(self):
  old=State(Bundle(6,b'old'))
  for rev in (3,0):
   state,status=accept(old,Bundle(rev,b'new'),old.url)
   self.assertEqual(status,'committed');self.assertEqual(state.bundle.revision,rev)
   self.assertEqual(state.generation,10);self.assertEqual(state.pin,old.pin)
 def test_same_revision_different_contents_are_saved(self):
  old=State(Bundle(3,b'old'));new=Bundle(3,b'new')
  state,status=accept(old,new,old.url)
  self.assertEqual(status,'committed');self.assertEqual(state.bundle,new)
 def test_all_revision_orders_obey_only_content_and_address(self):
  for before in (0,1,3,6,0xffffffff):
   for after in (0,1,3,6,0xffffffff):
    for body in (b'old',b'new'):
     with self.subTest(before=before,after=after,body=body):
      old=State(Bundle(before,b'old'));new=Bundle(after,body)
      actual,status=accept(old,new,old.url)
      self.assertEqual(status,'unchanged' if old.bundle.wire()==new.wire() else 'committed')
      self.assertEqual(actual.bundle,new)
 def test_identical_file_and_endpoint_never_writes(self):
  old=State(Bundle(3,b'old'));state=old
  for _ in range(100):
   state,status=accept(state,old.bundle,old.url)
   self.assertEqual(status,'unchanged')
  self.assertIs(state,old)
 def test_new_working_address_persists_even_with_identical_file(self):
  old=State(Bundle(3,b'old'));state,status=accept(old,old.bundle,'http://new:8443')
  self.assertEqual(status,'committed');self.assertEqual(state.url,'http://new:8443')
  self.assertEqual(state.bundle,old.bundle);self.assertEqual(state.pin,old.pin)
 def test_bad_file_identity_authentication_or_payload_preserves_old_state(self):
  old=State(Bundle(6,b'old'))
  for field in ('valid','identity','authenticated','payload'):
   incoming=replace(Bundle(3,b'new'),**{field:False})
   state,status=accept(old,incoming,'http://new:8443')
   self.assertIs(state,old);self.assertEqual(status,'rejected')
 def test_cancel_and_failed_commit_leave_old_slot_selected(self):
  old=State(Bundle(6,b'old'))
  for opts in ({'cancelled':True},{'commit_ok':False}):
   state,status=accept(old,Bundle(3,b'new'),old.url,**opts)
   self.assertIs(state,old);self.assertEqual(status,'rejected')
 def test_lower_primary_version_does_not_trigger_sd(self):
  old=State(Bundle(6,b'old'));new=Bundle(3,b'new')
  state,status,attempts=fallback(old,(new,old.url),(Bundle(8,b'ignored'),'http://sd:8443'))
  self.assertEqual(attempts,1);self.assertEqual(state.bundle,new);self.assertEqual(status,'committed')
 def test_sd_lower_version_is_accepted_only_after_primary_failure(self):
  old=State(Bundle(6,b'old'));new=Bundle(3,b'new')
  state,status,attempts=fallback(old,(Bundle(8,b'bad',valid=False),old.url),(new,'http://sd:8443'))
  self.assertEqual(attempts,2);self.assertEqual(status,'committed')
  self.assertEqual(state.bundle,new);self.assertEqual(state.url,'http://sd:8443')
 def test_both_failed_attempts_do_not_update_address_or_data(self):
  old=State(Bundle(6,b'old'))
  state,status,attempts=fallback(old,(Bundle(8,b'bad',identity=False),old.url),(Bundle(3,b'bad',authenticated=False),'http://sd:8443'))
  self.assertEqual(attempts,2);self.assertEqual(status,'rejected');self.assertIs(state,old)

if __name__=='__main__':unittest.main(verbosity=2)
