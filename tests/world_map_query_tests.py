"""Compile real query accessors with the new world-map context to detect aliases."""
from pathlib import Path
import re
import subprocess
import sys
import source_paths
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True,exist_ok=True)
src=(root/'src/search/SearchState.cpp').read_text()
decl=re.search(r'std::string\s+g_queries\[[^\]]+\];',src).group()
functions='\n'.join(source_paths.function(src, name) for name in ['std::string QueryOf(', 'void SetQuery('])
cpp=out/'map_query.cpp'
exe=out/'map_query.exe'
cpp.write_text(r'''
#include <iostream>
#include <mutex>
#include <string>
namespace menus {
enum class Menu { kNone,kInventory,kMagic,kContainerYours,kContainerTheirs,kQuests,kWorldMap,kCount };
constexpr std::size_t kMenuCount=static_cast<std::size_t>(Menu::kCount);
}
std::mutex g_qMutex;
''' + decl + '\n' + functions + r'''
int main() {
 for(int i=0;i<7;++i) SetQuery(static_cast<menus::Menu>(i),"query"+std::to_string(i));
 int failures=0;
 for(int i=0;i<7;++i) {
  if(QueryOf(static_cast<menus::Menu>(i))!="query"+std::to_string(i)) {
   ++failures; std::cerr<<"FAIL: query context "<<i<<" overwritten\n";
  }
 }
 if(failures) return 1;
 std::cout<<"7 independent query contexts passed\n";
}
''')
subprocess.run(['cl','/nologo','/std:c++20','/EHsc',str(cpp),'/Fo'+str(out/'query.obj'),'/Fe'+str(exe)],check=True)
subprocess.run([str(exe)],check=True)
