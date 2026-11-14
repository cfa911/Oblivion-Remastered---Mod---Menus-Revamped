// The preset's skin (see MenuSkin.h). Magic's last run-time recolours are baked into MRCustom_P (Step C.2).
#include "sorting/MenuSkin.h"
#include "core/Config.h"
namespace menuskin
{
	const uistyle::Skin& Current() { return uistyle::SkinFor(mrconfig::Get().stylePreset); }
}
