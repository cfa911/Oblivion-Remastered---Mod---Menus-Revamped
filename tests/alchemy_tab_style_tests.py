"""Native prefab integration guard; appearance still requires in-game acceptance."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import source_paths
s = source_paths.read('alchemy')
body = s[s.index('void LayoutFilterTabs('):s.index('void Layout(', s.index('void LayoutFilterTabs('))]
checks = [
    ('WBP_ModernMenu_Alchemy_FilterEffectButton' not in body and 'ui::CreateWidget' not in body, 'no native button instances'),
    ('MakeLabel' in body and 'uix::SetText(label, names[i])' in body, 'independent plain text labels'),
    ('SetButtonSelected' not in body and 'SetIsSelected' not in body, 'no native visual state calls'),
    ('g_hoverTab' in body, 'independent hover state'),
    ('g_tabLeft' in body and 'g_tabRight' in body, 'owned decorative images'),
    ('SetRenderTranslation' not in body, 'fixed canvas positions avoid cached-transform feedback'),
    ('g_tabDashLeft' in body and 'g_tabDashRight' in body, 'inactive tabs have their own dash artwork'),
    ('CopyTabFont(label' in body, 'use original tab font settings'),
    ('stroke < 3' in body, 'selected arrows are recreated from three owned strokes'),
    ('SetRenderScale' in body and 'side == 0 ? 1.0 : -1.0' in body, 'right ornaments mirror inward'),
    ('dashSize.w *= 0.8;' in body and 'dashSize.h *= 0.8;' in body, 'arrows and dashes reduced by twenty percent'),
]
for ok, why in checks:
    if not ok: print('FAIL', why)
print(f'{len(checks)} checks, {sum(not ok for ok, _ in checks)} failures')
raise SystemExit(any(not ok for ok, _ in checks))
