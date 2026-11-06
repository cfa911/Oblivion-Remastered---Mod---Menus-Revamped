"""Execute the actual hover bodies in settle mode ([Hover] uSettleMs > 0).

A sweep over rows must not deliver a hover per row (each delivered hover rebuilds the details panel, about 1,100
UObjects in Inventory); the row the pointer rests on is delivered once it has settled, the first hover after a rest
at once, and a repeat of the row already shown never.
"""
import ast
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
tree = ast.parse((root / "tests/hover_integration_tests.py").read_text(encoding="utf-8"))
prefix = next(ast.literal_eval(n.value) for n in tree.body
              if isinstance(n, ast.Assign)
              and any(isinstance(t, ast.Name) and t.id == "prefix" for t in n.targets))
source = source_paths.read("hover")
throttle = source[source.index("enum Target"):source.index("// ---- metrics")]

def extract(marker):
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

discover = extract("void Discover(")
tests = r"""
} }
UE::UFunction hover;
std::vector<UE::UObject*> shown;   // items that reached the native body
void Dispatch(UE::UObject* object,UE::UFunction* function,void* params) {
 if (!hoverfix::Filter(object,function,params)) shown.push_back(*static_cast<UE::UObject**>(params));
}
UE::UObject rows[12];
int main() {
 UE::dispatch=&Dispatch;
 hover.name="BndEvt__inv_cont_listview_K2Node_ComponentBoundEvent_OnItemHovered__DelegateSignature";
 int checks=0,failures=0;
 auto check=[&](bool ok,const char* name) {
  ++checks;if(!ok){++failures;std::cout<<"FAIL "<<name<<"\n";}
 };
 auto hoverRow=[&](int i){void* p=&rows[i];Dispatch(&inv,&hover,&p);};
 auto wait=[&](int ms){for(int t=0;t<ms;t+=10){clockMs+=10;hoverfix::Replay();}};
 settings.hoverSettleMs=100;settings.hoverIntervalMs=200;

 // First hover after a rest reaches the game at once.
 clockMs+=1000;hoverRow(0);
 check(shown.size()==1 && shown.back()==&rows[0],"first hover after rest is immediate");

 // A sweep (a new row every 30 ms for 300 ms) delivers nothing while it moves.
 for(int i=1;i<=10;++i){wait(30);hoverRow(i);}
 check(shown.size()==1,"no row is delivered during a sweep");

 // The row the pointer stops on is delivered once, after the settle time, not before.
 wait(90);
 check(shown.size()==1,"not delivered before the settle time");
 wait(20);
 check(shown.size()==2 && shown.back()==&rows[10],"resting row delivered after settle");
 wait(500);
 check(shown.size()==2,"settled row delivered exactly once");

 // Repeats of the row already shown never reach the game.
 hoverRow(10);wait(20);hoverRow(10);wait(300);
 check(shown.size()==2,"repeat of the shown row is not delivered again");

 // Leaving and coming back before settling cancels the row that was only passed.
 wait(300);
 hoverRow(5);   // after a rest: immediate
 check(shown.size()==3 && shown.back()==&rows[5],"next row after rest is immediate");
 wait(30);hoverRow(6);wait(30);hoverRow(5);wait(300);
 check(shown.size()==3,"returning to the shown row cancels the passed row");

 // A dead pending item is discarded.
 wait(300);hoverRow(1);wait(30);hoverRow(2);rows[2].live=false;wait(300);
 check(shown.size()==4 && shown.back()==&rows[1],"dead pending item is discarded");
 rows[2].live=true;

 // A key or button event in the menu delivers the held row first (quick keys and the details action button use the
 // last delivered hover), in Inventory and in Magic, in settle and in throttle mode.
 std::map<std::string,UE::UFunction> functions;   // one persistent UFunction per name, as in the game
 auto fire=[&](UE::UObject* menu,const char* name){
  auto& fn=functions[name];fn.name=name;
  for(auto& [cls,handler]:pe::watchedHandlers) if(cls==menu->cls) handler(menu,&fn,nullptr);
 };
 inv.cls=&mainClass;magic.cls=&magicClass;mainClass.name.name="WBP_ModernMenu_ContainerInventory_MainContent_C";
 hoverfix::Discover(menus::Menu::kInventory);hoverfix::Discover(menus::Menu::kMagic);
 hoverfix::g_state.clear();shown.clear();
 clockMs+=1000;hoverRow(7);wait(30);hoverRow(8);wait(30);
 fire(&inv,"Tick");
 check(shown.size()==1,"a non-input event does not flush the held row");
 fire(&inv,"InpActEvt_IA_UI_Specific_QuickKeys_Keyboard_1_K2Node_EnhancedInputActionEvent_9");
 check(shown.size()==2 && shown.back()==&rows[8],"quick key in Inventory delivers the held row before it runs");
 wait(300);
 check(shown.size()==2,"flushed row is not delivered again after settling");
 wait(300);hoverRow(9);wait(30);hoverRow(10);wait(30);
 fire(&inv,"BndEvt__WBP_OriginalMenu_Inventory_inv_item_details_K2Node_ComponentBoundEvent_4_OnActionClicked__DelegateSignature");
 check(shown.size()==4 && shown.back()==&rows[10],"details action button delivers the held row first");
 UE::UFunction magicHover;magicHover.name="BndEvt__WBP_ModernMenu_MagicMenu_Magic_Spell_List_K2Node_ComponentBoundEvent_7_OnItemHovered__DelegateSignature";
 auto hoverSpell=[&](int i){void* p=&rows[i];Dispatch(&magic,&magicHover,&p);};
 wait(300);hoverSpell(1);wait(30);hoverSpell(2);wait(30);
 fire(&magic,"InpActEvt_IA_UI_Specific_QuickKeys_Keyboard_3_K2Node_EnhancedInputActionEvent_7");
 check(shown.size()==6 && shown.back()==&rows[2],"quick key in Magic delivers the held spell first");
 settings.hoverSettleMs=0;hoverfix::g_state.clear();
 clockMs+=1000;hoverRow(3);clockMs+=10;hoverRow(4);
 fire(&inv,"InpActEvt_IA_UI_Specific_QuickKeys_Keyboard_2_K2Node_EnhancedInputActionEvent_8");
 check(shown.back()==&rows[4],"throttle mode also flushes before a key");
 settings.hoverSettleMs=100;

 // 0 keeps the time throttle: within the interval a second row is held, then replayed.
 settings.hoverSettleMs=0;hoverfix::g_state.clear();shown.clear();
 clockMs+=1000;hoverRow(3);clockMs+=10;hoverRow(4);
 check(shown.size()==1,"settle 0 keeps the interval throttle (held)");
 clockMs+=201;hoverfix::Replay();
 check(shown.size()==2 && shown.back()==&rows[4],"settle 0 keeps the interval throttle (replayed)");

 std::cout<<checks<<" checks, "<<failures<<" failures\n";
 return failures?1:0;
}
"""
cpp = out / "hover_settle_tests.cpp"
cpp.write_text(prefix + throttle + discover + tests, encoding="utf-8")
exe = out / "hover_settle_tests.exe"
subprocess.run(["cl", "/nologo", "/EHsc", "/std:c++latest", str(cpp),
                f"/Fe:{exe}", f"/Fo:{out / 'hover_settle_tests.obj'}"], check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
