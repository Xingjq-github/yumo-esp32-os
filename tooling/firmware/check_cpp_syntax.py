"""Read-only Tree-sitter C/C++ syntax scan; never invokes a compiler or a device.

The caller supplies an installed tree_sitter_cpp package or a temporary official
Windows grammar extension with --cpp-binding. No automatic downloads/installs.
"""
from pathlib import Path
import argparse,importlib.util,json

def main():
    from tree_sitter import Language,Parser
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpp-binding',type=Path)
    ap.add_argument('--report',type=Path)
    ap.add_argument('files',nargs='+',type=Path)
    args=ap.parse_args()
    if args.cpp_binding:
        spec=importlib.util.spec_from_file_location('tree_sitter_cpp._binding',args.cpp_binding.resolve())
        cpp=importlib.util.module_from_spec(spec);spec.loader.exec_module(cpp)
    else:import tree_sitter_cpp as cpp
    import tree_sitter_c as c
    parsers={'.c':Parser(Language(c.language())),'.cpp':Parser(Language(cpp.language()))}
    entries=[]
    for path in args.files:
        source=path.read_bytes();tree=parsers['.c' if path.suffix=='.c' else '.cpp'].parse(source)
        problems=[];nodes=[tree.root_node]
        while nodes:
            node=nodes.pop()
            if node.type=='ERROR' or node.is_missing:
                problems.append(dict(line=node.start_point.row+1,column=node.start_point.column+1,type=node.type,text=source[node.start_byte:min(node.end_byte,node.start_byte+120)].decode('utf-8','replace')))
            nodes.extend(reversed(node.children))
        entries.append(dict(path=path.as_posix(),errors=problems))
    report=dict(passed=not any(item['errors'] for item in entries),files_checked=len(entries),files=entries,compile_performed=False)
    if args.report:args.report.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2))
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())