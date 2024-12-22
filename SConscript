Import('RTT_ROOT')
Import('rtconfig')
from building import *

cwd     = GetCurrentDir()
src     = Glob('*.c')
CPPPATH = [
  cwd,
]

CFLAGS = ' -c -ffunction-sections'

group   = DefineGroup('Applications', src, depend = [''], CPPPATH = CPPPATH, CFLAGS=CFLAGS)

group = group + SConscript(os.path.join(cwd, 'src/core/SConscript'))
group = group + SConscript(os.path.join(cwd, 'src/core/ipv4/SConscript'))
group = group + SConscript(os.path.join(cwd, 'src/core/ipv6/SConscript'))

Return('group')
