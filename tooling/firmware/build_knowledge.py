"""Build/check the offline SD knowledge package. Never invokes any compiler.

Source modules contain original knowledge cards, not executable device code.
The binary layout is mirrored by main/core/knowledge_format.hpp.
"""
from pathlib import Path
import argparse,hashlib,importlib.util,json,re,struct,zlib
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
CATEGORIES=('Java','JVM','算法','MySQL','组件','工具')
HEADER=64;ENTRY=48;SECTION=24;MAX_BLOCK=8192
def crc(data):return zlib.crc32(data)&0xffffffff
def sid(text):return int.from_bytes(hashlib.sha256(text.encode('utf-8')).digest()[:8],'little') or 1
def section_id(text):return sid(text)&0xffffffff or 1
def initials(text):
    # GB2312 first-level Hanzi are ordered by pinyin. Polyphonic names can add
    # explicit aliases in source cards; English titles/IDs remain searchable.
    bounds=(-20319,-20284,-19776,-19219,-18711,-18527,-18240,-17923,-17418,-16475,-16213,-15641,-15166,-14923,-14915,-14631,-14150,-14091,-13319,-12839,-12557,-11848,-11056)
    letters='ABCDEFGHJKLMNOPQRSTWXYZ';out=''
    for c in text:
        if c.isascii():
            if c.isalnum():out+=c.lower()
            continue
        try:raw=c.encode('gb2312')
        except UnicodeEncodeError:continue
        if len(raw)!=2:continue
        value=raw[0]*256+raw[1]-65536
        if bounds[0]<=value<=-10247:
            out+=letters[max(i for i,b in enumerate(bounds) if value>=b)].lower()
    return out
def sources():
    import sys
    location=ROOT/'../resources/knowledge';sys.path.insert(0,str(location))
    from java_cards import cards as java
    from jvm_cards import cards as jvm
    from algorithm_cards import cards as algorithms
    from mysql_cards import cards as mysql
    from component_cards import cards as components
    from tool_cards import cards as tools
    from pattern_cards import cards as patterns
    from basic_cards import cards as basics
    from windows_cards import cards as windows
    from kubernetes_cards import cards as kubernetes
    cards=java()+patterns()+jvm()+algorithms()+mysql()+components()+tools()+basics()+windows()+kubernetes()
    # Surface practical starting points before advanced topics, retaining IDs
    # so existing SD favorites and reading marks survive content updates.
    first={'基础操作','启动与诊断','入门与命令'}
    cards.sort(key=lambda c:(CATEGORIES.index(c['category']),c['group'] not in first))
    links={
        'java.byte':['java.promotion','java.conversion'],
        'java.char':['java.string','java.substring'],
        'java.boolean':['java.logic'],
        'java.defaults':['java.nullpointer','java.references'],
        'java.boxing':['java.equality','java.nullpointer'],
        'java.equality':['java.equals-hashcode','java.string'],
        'java.interface':['java.polymorphism','java.pattern.strategy'],
        'java.inheritance':['java.composition','java.override'],
        'java.string':['java.stringbuilder','java.regex'],
        'java.hashmap':['java.equals-hashcode','java.concurrent-map','algorithm.hash'],
        'java.list':['java.arraylist','java.linkedlist','java.immutable-list'],
        'java.generic-class':['java.wildcard-extends','java.wildcard-super','java.erasure'],
        'java.exceptions':['java.try-resources','java.exception-chain'],
        'java.files':['java.try-resources'],
        'java.thread':['java.executor','java.virtual-thread'],
        'java.synchronized':['java.volatile','java.lock','jvm.deadlock'],
        'java.volatile':['java.atomic','java.synchronized'],
        'java.stream':['java.lambda','java.optional'],
        'jvm.heap':['jvm.xmx','jvm.oom-heap'],
        'jvm.metaspace':['jvm.max-metaspace','jvm.classloader'],
        'jvm.gc-frequent':['jvm.gc-log','jvm.jfr'],
        'jvm.cpu-high':['jvm.thread-dump','jvm.jfr'],
        'jvm.deadlock':['java.lock','java.synchronized'],
        'mysql.join':['mysql.left-join','mysql.explain'],
        'mysql.transaction':['mysql.isolation','mysql.deadlock'],
        'mysql.window':['mysql.rank','mysql.lag'],
        'mysql.composite-index':['mysql.covering','mysql.explain-analyze'],
        'component.mysql':['mysql.create-table','component.replication'],
        'component.kafka':['component.idempotency','component.retry'],
        'component.redis':['component.cache','component.replication'],
        'component.docker':['component.kubernetes'],
        'component.sqlite':['component.mysql'],
        'tool.maven.dependencies':['tool.maven.lifecycle'],
        'tool.http.methods':['component.idempotency','tool.http.status'],
    }
    for c in cards:
        c['related']=list(dict.fromkeys(c.get('related',[])+links.get(c['id'],[])))
    return cards
def validate(cards):
    ids=set();identities=set()
    from ttf2lvgl import build_charset
    glyphs=set(build_charset())|{9,10}
    for c in cards:
        assert re.fullmatch(r'[a-z][a-z0-9.-]{2,80}',c['id']),c['id']
        assert c['id'] not in ids and sid(c['id']) not in identities,c['id']
        ids.add(c['id']);identities.add(sid(c['id']))
        assert c['category'] in CATEGORIES and c['group'] and c['version'] and c['source']
        assert c['source'].startswith('https://'),c['id']
        assert len(c['sections']) in range(2,33) and len(c.get('related',[]))<=8
        section_ids=set()
        for s in c['sections']:
            assert s['id'] not in section_ids,(c['id'],s['id']);section_ids.add(s['id'])
            data=s['text'].encode('utf-8');assert 0<len(data)<=MAX_BLOCK,(c['id'],s['title'],len(data))
            assert len(s['text'].split('\n'))<=128 and '\t' not in s['text'],(c['id'],'line or tab bound')
            assert '\r' not in s['text'] and '\x00' not in s['text']
            assert not (set(map(ord,s['text']+s['title']))-glyphs),(c['id'],'missing font glyph',set(map(ord,s['text']+s['title']))-glyphs)
            if s.get('kind')=='code':
                assert s['text'].isascii(),(c['id'],'code uses ASCII mono font')
                assert '\t' not in s['text'],(c['id'],'expand tabs in source')
                assert max(map(len,s['text'].splitlines()))<=240,(c['id'],'line exceeds viewer bound')
        assert all(len(c[k].encode('utf-8'))<=512 for k in ('title','group','version','source'))
    for c in cards:
        assert all(x in ids and x!=c['id'] for x in c.get('related',[])),(c['id'],'related target')
    return ids
def body(c):
    sections=c['sections'];related=c.get('related',[]);base=16+len(sections)*SECTION+len(related)*8
    names=bytearray();name_offsets=[]
    for s in sections:name_offsets.append(base+len(names));names+=s['title'].encode()+b'\0'
    toc=base+len(names);assert toc<=4096
    payload=bytearray();records=bytearray()
    for s,title in zip(sections,name_offsets):
        data=s['text'].encode();kind=int(s.get('kind')=='code');lang={'text':0,'java':1,'sql':2,'shell':3,'json':4}[s.get('language','text')]
        records+=struct.pack('<IIIIIBBH',section_id(s['id']),toc+len(payload),len(data),crc(data),title,kind,lang,s.get('first_line',1))
        payload+=data
    return struct.pack('<4sHHII',b'YKE1',len(sections),len(related),toc,0)+records+b''.join(struct.pack('<Q',sid(x)) for x in related)+names+payload
def build(cards,revision=20261030):
    validate(cards);assert 0<len(cards)<=1024
    index=bytearray(len(cards)*ENTRY);strings=bytearray();offsets={};data=bytearray()
    def string(s):
        if s not in offsets:offsets[s]=len(index)+len(strings);strings.extend(s.encode('utf-8')+b'\0')
        return offsets[s]
    for i,c in enumerate(cards):
        aliases=' '.join([c['title'],c.get('short_title',c['title']),initials(c['title']),c['id'],*c.get('aliases',[]),*c.get('tags',[])]).lower()
        assert len(aliases.encode())<=1024
        b=body(c)
        struct.pack_into('<QIIIIIIIHHII',index,i*ENTRY,sid(c['id']),string(c['title']),string(c['group']),string(aliases),string(c['version']),len(data),len(b),crc(b),CATEGORIES.index(c['category']),len(c['sections']),string(c.get('short_title',c['title'])),string(c['source']))
        data+=b
    index+=strings;assert len(index)<=128*1024 and len(data)<=8*1024*1024
    h=bytearray(HEADER);struct.pack_into('<8sIIIIIII',h,0,b'YUMOREF1',1,revision,len(cards),len(index),len(data),crc(index),crc(data));struct.pack_into('<I',h,60,crc(h[:60]))
    return bytes(h+index+data)
def inspect(blob):
    """Independent strict decoder for package round trips/corruption tests."""
    if len(blob)<64:raise ValueError('short header')
    magic,version,revision,count,ni,nd,ci,cd=struct.unpack_from('<8sIIIIIII',blob)
    if magic!=b'YUMOREF1' or version!=1 or crc(blob[:60])!=struct.unpack_from('<I',blob,60)[0]:raise ValueError('header')
    if not 0<count<=1024 or not count*48<=ni<=128*1024 or not 0<nd<=8*1024*1024 or len(blob)!=64+ni+nd:raise ValueError('bounds')
    idx=blob[64:64+ni];data=blob[64+ni:]
    if crc(idx)!=ci or crc(data)!=cd:raise ValueError('crc')
    out=[];end=0;seen=set()
    def text_at(offset,limit=512):
        if not count*48<=offset<len(idx):raise ValueError('string offset')
        stop=idx.find(b'\0',offset,min(len(idx),offset+limit+1))
        if stop<0:raise ValueError('string terminator')
        return idx[offset:stop].decode('utf-8')
    for i in range(count):
        ident,t,g,a,v,offset,length,checksum,category,ns,short,source=struct.unpack_from('<QIIIIIIIHHII',idx,i*48)
        if not ident or ident in seen or category>=6 or not 0<ns<=32 or offset!=end or offset+length>nd:raise ValueError('entry')
        seen.add(ident);end=offset+length;b=data[offset:end]
        if len(b)<16 or crc(b)!=checksum:raise ValueError('entry crc')
        mark,sc,nr,toc,_=struct.unpack_from('<4sHHII',b)
        minimum=16+sc*24+nr*8
        if mark!=b'YKE1' or sc!=ns or nr>8 or not minimum<=toc<=min(4096,len(b)):raise ValueError('toc')
        sections=[];last=toc;section_ids=set()
        for j in range(sc):
            ident_s,at,n,check,title,kind,lang,first=struct.unpack_from('<IIIIIBBH',b,16+j*24)
            if not ident_s or ident_s in section_ids or at!=last or not 0<n<=8192 or at+n>len(b) or kind>1 or lang>4 or not minimum<=title<toc:raise ValueError('block bounds')
            section_ids.add(ident_s);zero=b.find(b'\0',title,min(toc,title+97))
            if zero<0:raise ValueError('section title')
            raw=b[at:at+n]
            if crc(raw)!=check:raise ValueError('block crc')
            s=raw.decode('utf-8')
            if '\x00' in s or any(ord(c)<32 and c not in '\n\t' for c in s):raise ValueError('control character')
            if len(s.split('\n'))>128 or '\t' in s:raise ValueError('line bound')
            if kind and (not s.isascii() or max(map(len,s.splitlines()))>240):raise ValueError('code font/width')
            sections.append(dict(id=ident_s,title=b[title:zero].decode('utf-8'),text=s,kind=kind,language=lang,first_line=first));last=at+n
        if last!=len(b):raise ValueError('trailing body')
        out.append(dict(id=ident,title=text_at(t),group=text_at(g),aliases=text_at(a,1024),version=text_at(v),short_title=text_at(short),source=text_at(source),category=category,sections=sections))
    if end!=nd:raise ValueError('trailing data')
    return dict(revision=revision,count=count,index_bytes=ni,data_bytes=nd,entries=out)
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--output',type=Path,default=ROOT/'../resources/sdcard/programming/library.ykb');ap.add_argument('--check',action='store_true');args=ap.parse_args()
    cards=sources();blob=build(cards);info=inspect(blob)
    counts={c:sum(x['category']==c for x in cards) for c in CATEGORIES}
    report=dict(format=1,revision=info['revision'],entries=len(cards),categories=counts,java_design_patterns=sum(x['id'].startswith('java.pattern.') for x in cards),bytes=len(blob),index_bytes=info['index_bytes'],sha256=hashlib.sha256(blob).hexdigest(),compile_performed=False)
    if args.check:
        assert args.output.read_bytes()==blob,'Package is stale; regenerate it'
    else:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(blob)
        metadata=ROOT.parent/'artifacts/resources/programming';metadata.mkdir(parents=True,exist_ok=True)
        (metadata/'catalog.json').write_text(json.dumps(dict(**report,contents=[{k:c[k] for k in ('id','title','category','group','version','source')} for c in cards]),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
