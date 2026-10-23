"""Original source cards. Short table fields: id ~~ title ~~ overview ~~ example ~~ pitfall.

Examples marked snippets are deliberately not advertised as complete programs.
Complete algorithm/pattern examples explicitly provide their surrounding class.
"""
import re
JLS='https://docs.oracle.com/javase/specs/jls/se21/html/'
API='https://docs.oracle.com/en/java/javase/21/docs/api/'
JVM='https://docs.oracle.com/en/java/javase/21/docs/specs/man/java.html'
MYSQL='https://dev.mysql.com/doc/refman/8.4/en/'
def section(ident,title,text,code=False,language='java'):
    return dict(id=ident,title=title,text=text.strip('\n'),kind='code' if code else 'text',language=language if code else 'text')
def make(ident,title,category,group,overview,example,pitfall,source,version,aliases=(),related=(),language='java',complete=False,extra=()):
    sections=[section('overview','速查',overview)]
    sections+=list(extra)
    if example:sections.append(section('example','完整示例' if complete else '示例片段',example,True,language))
    sections.append(section('pitfall','注意事项',pitfall))
    sections.append(section('reference','版本与出处',version+'\n参考：'+source+'\n内容为独立整理；示例仅供阅读，设备不执行代码。'))
    return dict(id=ident,title=title,short_title=title,category=category,group=group,version=version,source=source,revision=20260928,aliases=list(aliases),tags=[group],related=list(related),sections=sections)
def table(text,category,group,source,version,prefix,language='java'):
    result=[]
    for line in text.strip().splitlines():
        if not line.strip() or line.startswith('#'):continue
        values=line.split(' ~~ ')
        if len(values)!=5:raise ValueError((prefix,line))
        slug,title,overview,example,pitfall=values
        example=re.sub(r'\\\\|\\n',lambda m:'\\' if m[0]=='\\\\' else '\n',example)
        result.append(make(prefix+slug,title,category,group,overview,example,pitfall,source,version,aliases=[slug],language=language))
    return result
