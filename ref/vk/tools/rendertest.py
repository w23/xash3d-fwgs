#!/usr/bin/env python3

displays = {
	'full': '',
	'basecolor': 'basecolor',
	'emissive': 'emissive',
	'nshade': 'nshade',
	'ngeom': 'ngeom',
	'lighting': 'lighting',
	'direct': 'direct',
	'indirect': 'indirect',
	'indirect_spec': 'indirect_spec',
	'indirect_diff': 'indirect_diff',
}

saves = [
	'brush2_01',
	'brush_01',
	'brush_02',
	'c0a0d_emissive',
	'light_01',
]

header = '''m_ignore 1
scr_conspeed 100000
con_notifytime 0
hud_draw 0
r_speeds 0
developer 0'''

print(header)

for save in saves:
	screenshot_base = 'rendertest/'
	print('')
	print(f'load rendertest_{save}; wait 20')
	for name, display in displays.items():
		print(f'rt_debug_display_only "{display}"; screenshot {screenshot_base}{save}_{name}.png; wait 1')

print('quit')
