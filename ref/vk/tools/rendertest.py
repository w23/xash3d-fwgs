#!/usr/bin/env python3

saves = [
	'brush2_01',
	'brush_01',
	'brush_02',
	'c0a0d_emissive',
	'light_01',
]

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

import argparse

parser = argparse.ArgumentParser(description='Generate scripts and makefiles for rendertest')
parser.add_argument('--script', '-s', type=argparse.FileType('w'), help='Console script for generating images')
args = parser.parse_args()

def make_script(file):
	header = '''m_ignore 1
scr_conspeed 100000
con_notifytime 0
hud_draw 0
r_speeds 0
rt_debug_fixed_random_seed 31337
developer 0
'''

	file.write(header)

	for save in saves:
		screenshot_base = 'rendertest/'
		file.write(f'\nload rendertest_{save}; wait 20')
		for name, display in displays.items():
			file.write(f'rt_debug_display_only "{display}"; screenshot {screenshot_base}{save}_{name}.png; wait 1\n')

	file.write('\nquit\n')

if args.script:
	print(f'Generating script {args.script.name}')
	make_script(args.script)
