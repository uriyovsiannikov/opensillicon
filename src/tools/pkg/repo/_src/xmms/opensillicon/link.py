from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parents[7]
os.chdir(root)
b=root/'src/build/xmms';src=root/'src/tools/pkg/repo/_src/xmms';cc=src/'opensillicon/cc'
env=dict(os.environ,OPEN_SILLICON_ROOT=str(root))
inc=['-I'+str(b/'player'),'-I'+str(src/'upstream'),'-I'+str(b/'prefix/include/glib-1.2'),'-I'+str(b/'prefix/lib/glib/include')]
objects=[]
for path in [src/'opensillicon/output.c',*sorted((src/'opensillicon/runtime').glob('*.c')),src/'opensillicon/runtime/context.S']:
 out=b/(path.stem+'.o');subprocess.run([str(cc),*inc,'-c',str(path),'-o',str(out)],env=env,check=True);objects.append(str(out))
libraries=[b/'player/libxmms/.libs/libxmms.a',b/'player/Input/wav/.libs/libwav.a',b/'player/Input/mpg123/.libs/libmpg123.a',*[b/('prefix/lib/lib'+n+'.a') for n in ['gtk','gdk','gmodule','glib']],*[root/('src/build/x11/prefix/lib/lib'+n+'.a') for n in ['Xext','X11','xcb','Xau']],root/'src/build/libz/libz.a',root/'src/build/libos/liblibos.a']
subprocess.run(['gcc','-static','-no-pie','-nostdlib','-Wl,-z,noexecstack','-Wl,--wrap=poll','-Wl,--wrap=usleep','-Wl,--wrap=select','-Wl,-T,src/user/c_linker.ld','src/build/libos/crt0.o',*map(str,sorted((b/'player/xmms').glob('*.o'))),*objects,'-Wl,--start-group',*map(str,libraries),'-lgcc','-Wl,--end-group','-o',str(b/'xmms')],check=True)
