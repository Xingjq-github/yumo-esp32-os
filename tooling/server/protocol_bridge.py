"""Java test-class bridge. Fixed JUnit vectors independently cover wire compatibility."""
import base64,json,os,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
def relocated_classpath(classpath, previous_root, current_root):
    """Map only entries under the recorded Maven project, not external SDK paths."""
    previous_root=Path(previous_root);current_root=Path(current_root).resolve()
    if not classpath:raise RuntimeError('Invalid empty test classpath; run Maven package again')
    mapped=[]
    for entry in classpath.split(os.pathsep):
        # An empty entry uses the child process cwd, which is the current server root.
        path=Path(entry) if entry else current_root
        if not path.is_absolute():path=previous_root/path
        try:path=current_root/path.relative_to(previous_root)
        except ValueError:pass
        mapped.append(str(path))
    return os.pathsep.join(mapped)

class ProtocolBridge:
    def __init__(self,java,server):
        report=Path(server).parent/'artifacts/server/surefire-reports/TEST-com.joyvault.CompatibilityTest.xml'
        if not report.is_file():raise RuntimeError('Run Maven package (including tests) first')
        props={p.attrib['name']:p.attrib['value'] for p in ET.parse(report).findall('./properties/property')}
        self.java=java;self.server=Path(server).resolve()
        self.cp=relocated_classpath(props['java.class.path'],props.get('basedir',str(self.server)),self.server)
        if not all(Path(entry).exists() for entry in self.cp.split(os.pathsep)):
            raise RuntimeError('Java test classpath is incomplete; run Maven package again')
    def call(self,op,keys,**values):
        result=subprocess.run([self.java,'-Xmx64m','-XX:ActiveProcessorCount=1','-XX:+UseSerialGC','-cp',self.cp,'com.joyvault.ProtocolFixtureCli'],
            input=json.dumps(dict(op=op,keys=keys,**values)).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,
            cwd=self.server,timeout=15,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if result.returncode:raise RuntimeError('Synthetic protocol bridge failed; no key output recorded')
        return json.loads(result.stdout)
    def encode(self,entries,revision,keys):return base64.b64decode(self.call('encode',keys,entries=entries,revision=revision))
    def decode(self,blob,keys):return self.call('decode',keys,blob=base64.b64encode(blob).decode())
