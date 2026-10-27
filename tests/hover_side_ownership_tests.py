"""Execute actual hover bodies: a previous container side must not steal focus."""
import ast
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
# Reuse the existing native-widget doubles without importing/running that test.
tree = ast.parse((root / "tests/hover_integration_tests.py").read_text(encoding="utf-8"))
prefix = next(ast.literal_eval(n.value) for n in tree.body
              if isinstance(n, ast.Assign)
              and any(isinstance(t, ast.Name) and t.id == "prefix" for t in n.targets))
source = (root / "revamped/HoverFix.cpp").read_text(encoding="utf-8")
throttle = source[source.index("enum Target"):source.index("// ---- metrics")]
tests = r"""
} }
UE::UFunction hover;
UE::UObject* activeSide=nullptr;
int deliveries=0;
void Dispatch(UE::UObject* object,UE::UFunction* function,void* params) {
 if (!hoverfix::Filter(object,function,params)) {
  activeSide=object;++deliveries;
 }
}
int main() {
 UE::dispatch=&Dispatch;
 hover.name="BndEvt__inv_cont_listview_K2Node_ComponentBoundEvent_OnItemHovered__DelegateSignature";
 int checks=0,failures=0;
 auto check=[&](bool ok,const char* name) {
  ++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<"\n";}
 };
 void* a=&itemA;void* b=&itemB;
 containerOpen=true;
 for(int reverse=0;reverse<2;++reverse) {
  auto* left=reverse?&theirs:&yours;
  auto* right=reverse?&yours:&theirs;
  hoverfix::g_state.clear();clockMs+=1000;
  Dispatch(left,&hover,&a);clockMs+=10;
  Dispatch(left,&hover,&b); // left now has a pending hover
  clockMs+=10;Dispatch(right,&hover,&a); // actual input has switched sides
  check(activeSide==right,"real side change reaches the native body");
  const int before=deliveries;
  clockMs+=181;hoverfix::Replay(); // old side's deadline, no further input
  check(activeSide==right,"old-side deferred hover cannot briefly steal active focus");
  check(deliveries==before,"superseded old-side pending hover is discarded");
  clockMs+=401;Dispatch(left,&hover,&a);
  check(activeSide==left,"real input can switch back to the previous side");
 }
 // Both receivers still have independent timers outside a two-sided container.
 containerOpen=false;hoverfix::g_state.clear();clockMs+=1000;
 Dispatch(&yours,&hover,&a);Dispatch(&theirs,&hover,&a);
 clockMs+=10;Dispatch(&yours,&hover,&b);Dispatch(&theirs,&hover,&b);
 const int before=deliveries;clockMs+=201;hoverfix::Replay();
 check(deliveries==before+2,"non-container per-widget replay remains independent");
 std::cout<<checks<<" checks, "<<failures<<" failures\n";
 return failures?1:0;
}
"""
cpp = out / "hover_side_ownership_tests.cpp"
cpp.write_text(prefix + throttle + tests, encoding="utf-8")
exe = out / "hover_side_ownership_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'hover_side_ownership_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
