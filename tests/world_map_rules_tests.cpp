#define NOMINMAX
#include <Windows.h>
#include <iostream>
#include "search/WorldMapRules.h"
int main()
{
	int checks=0, failed=0;
	auto check=[&](bool yes,const char* name){ ++checks; if(!yes){ ++failed; std::cerr<<"FAIL "<<name<<'\n'; } };
	using maprules::Location;
	std::vector<Location> locations{
		{"b","same-key","ref-b","Fort Café",20,30,5,true},
		{"a","same-key","ref-a","Fort Café",10,30,5,true},
		{"c","key-c","ref-c","Castle Anvil",40,30,3,true},
		{"hidden","key-h","ref-h","Hidden Fort",0,0,5,false},
		{"player","key-p","","Player Fort",0,0,14,true},
		{"quest","key-q","","Quest Fort",0,0,13,true},
		{"empty","key-e","","",0,0,3,true}
	};
	check(maprules::Eligible(locations[0]),"discovered location included");
	check(!maprules::Eligible(locations[3]),"undiscovered marker excluded");
	check(!maprules::Eligible(locations[4]),"player marker excluded");
	check(!maprules::Eligible(locations[5]),"quest marker excluded");
	check(!maprules::Eligible(locations[6]),"empty name excluded");
	auto matches=maprules::Filter(locations,"FORT",true);
	check(matches.size()==2,"case insensitive partial match and discovery filtering");
	check(matches.size()==2 && matches[0].id=="a" && matches[1].id=="b","duplicate names/keys retain distinct stable identities");
	check(maprules::Filter(locations,"cafe",true).size()==2,"plain query matches accents");
	check(maprules::Filter(locations,"cafe",false).empty(),"accent option respected");
	check(maprules::Filter(locations,"",true).empty(),"empty query hides results");
	check(maprules::Filter(locations,"missing",true).empty(),"no results");
	auto all=maprules::Filter(locations,"a",true);
	check(!all.empty() && all[0].name=="Castle Anvil","alphabetical ordering");
	check(maprules::Resolve(locations,"b")==&locations[0],"selection resolves exact identity");
	check(maprules::Resolve(locations,"removed")==nullptr,"stale selection rejected");
	check(maprules::Resolve(locations,"hidden")==nullptr,"selection revalidates discovery");
	locations[0].passesNativeFilter=false;
	check(maprules::Filter(locations,"fort",true).size()==1,"inactive native type excludes matching marker");
	check(maprules::Resolve(locations,"b")==nullptr,"selection revalidates native filter state");
	locations[1].passesNativeFilter=false;
	check(maprules::Filter(locations,"fort",true).empty(),"all matching types disabled yields no results");
	locations[0].passesNativeFilter=true;
	check(maprules::Filter(locations,"fort",true).size()==1,"re-enabled native type returns with retained query");
	std::cout<<checks<<" checks, "<<failed<<" failures\n";
	return failed?1:0;
}
