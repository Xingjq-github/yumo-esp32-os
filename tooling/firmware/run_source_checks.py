"""Run current source contracts and Python models; never compiles or touches devices."""
from pathlib import Path
import argparse,json,sys,unittest
ROOT=(Path(__file__).resolve().parents[2]/'firmware')
def flatten(suite):
    for test in suite:
        if isinstance(test,unittest.TestSuite):yield from flatten(test)
        else:yield test
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report',type=Path,help='Optional JSON summary path')
    args=parser.parse_args()
    sys.path.insert(0,str(ROOT/'../tooling/firmware'))
    def readonly_guard(event,values):
        if event in ('subprocess.Popen','os.system','socket.connect','socket.bind'):
            raise RuntimeError('Source-only check cannot start subprocesses or network operations')
    sys.addaudithook(readonly_guard)
    excluded=json.loads((ROOT/'../tooling/firmware/fixtures/superseded-contracts.json').read_text(encoding='utf-8'))
    tests=list(flatten(unittest.defaultTestLoader.discover(str(ROOT/'../tooling/firmware'),pattern='check_*.py')))
    if not set(excluded)<={test.id() for test in tests}:
        raise RuntimeError('Historical exclusions no longer match available tests')
    selected=[test for test in tests if test.id() not in excluded]
    result=unittest.TextTestRunner(verbosity=2).run(unittest.TestSuite(selected))
    summary=dict(discovered=len(tests),executed=result.testsRun,existing_exclusions=len(excluded),
        passed=result.wasSuccessful(),failures=[test.id() for test,_ in result.failures],
        errors=[test.id() for test,_ in result.errors],skipped=len(result.skipped),
        compile_performed=False,device_or_serial_accessed=False)
    if args.report:args.report.write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print(json.dumps(summary,indent=2))
    return 0 if result.wasSuccessful() else 1
if __name__=='__main__':raise SystemExit(main())
