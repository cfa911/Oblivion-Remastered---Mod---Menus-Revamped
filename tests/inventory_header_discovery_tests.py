"""Execute the real Inventory-header lookup against the existing named-list contract."""
from pathlib import Path
import re
import subprocess
import sys
import source_paths

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=(root/'src/sorting/InventoryHeaders.cpp').read_text()
lookup=source_paths.function(source,'UE::UObject* InventoryList(')
if not lookup:
    layout=source_paths.function(source,'bool Picker::LayoutInventoryHeaders(')
    statement=re.search(r'auto\* list\s*=.*?;',layout).group(0)
    lookup='UE::UObject* InventoryList(UE::UObject* a_menu){'+statement+'return list;}'
code=r'''
#include <cstdio>
#include <string_view>
namespace UE { struct UObject{}; }
UE::UObject menu,named,fallback;
bool hasNamed=true,hasFallback=false;
namespace ui {
UE::UObject* FindInTree(UE::UObject*,std::string_view name){return hasNamed&&name=="inv_cont_listview"?&named:nullptr;}
UE::UObject* FindByClassPrefix(UE::UObject*,std::string_view){return hasFallback?&fallback:nullptr;}
}
'''+lookup+r'''
int main(){unsigned checks=0,bad=0;auto check=[&](bool ok,const char* why){++checks;if(!ok){++bad;std::printf("FAIL %s\n",why);}};
check(InventoryList(&menu)==&named,"named native list is found even when its class differs");
hasFallback=true;check(InventoryList(&menu)==&named,"established named lookup takes precedence");
hasNamed=false;check(InventoryList(&menu)==&fallback,"class fallback remains available");
hasFallback=false;check(InventoryList(&menu)==nullptr,"unavailable list returns safely");
std::printf("%u checks, %u failures\n",checks,bad);return bad?1:0;}
'''
cpp=out/'discovery.cpp';exe=out/'discovery.exe';cpp.write_text(code)
subprocess.run(['cl','/nologo','/EHsc','/std:c++latest',str(cpp),f'/Fe:{exe}',f'/Fo:{out}/'],check=True)
sys.exit(subprocess.run([str(exe)]).returncode)
