#!/usr/bin/env python3
"""Build the original XMMS 1.2.11 GUI and static WAV/MP3 plugins for libos."""
from pathlib import Path
import os,re,subprocess
ROOT=Path(__file__).resolve().parents[7]
SRC=ROOT/'src/tools/pkg/repo/_src/xmms'
BUILD=ROOT/'src/build/xmms'
PREFIX=BUILD/'prefix'
CC=SRC/'opensillicon/cc'
env=dict(os.environ,OPEN_SILLICON_ROOT=str(ROOT),CC=str(CC))
for key,value in {'char':1,'short':2,'int':4,'long':8,'long_long':8,'void_p':8}.items():env['ac_cv_sizeof_'+key]=str(value)
env['ac_cv_func_atexit']='yes';env['ac_cv_func_vsnprintf']='yes'
env['ac_cv_func_pthread_attr_init']='yes'
JOBS=str(min(os.cpu_count() or 1,8))
def run(args,cwd,log):
 cwd.mkdir(parents=True,exist_ok=True)
 with (cwd/log).open('w') as f:
  p=subprocess.run(list(map(str,args)),cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if p.returncode:
  print((cwd/log).read_text(errors='replace')[-8000:]);raise SystemExit(p.returncode)
def configure(name,source,args):
 path=BUILD/name;path.mkdir(parents=True,exist_ok=True)
 if not (path/'Makefile').exists():
  run([source/'configure','--host=x86_64-unknown-linux-gnu','--build=x86_64-pc-linux-gnu','--disable-shared','--enable-static',*args],path,'configure.log')
 return path
def make(path,*targets):run(['make','-j'+JOBS,*targets],path,'build.log')
def edit_config(path):
 s=path.read_text()
 for key in ['HAVE_FTS_H','HAVE_SCHED_H','HAVE_SYS_SCHED_H','HAVE_SYS_SYSCTL_H','HAVE_SYS_PARAM_H','HAVE_MNTENT_H','HAVE_GETMNTENT']:
  s=s.replace('#define '+key+' 1','/* #undef '+key+' */')
 path.write_text(s)
glib=configure('glib',SRC/'deps/glib-1.2.10',['--prefix='+str(PREFIX),'--disable-threads'])
p=glib/'glibconfig.h';p.write_text(re.sub(r'(#define GLIB_SYSDEF_[A-Z]+) (?![=])',r'\1 =',p.read_text()))
p=glib/'config.h';s=p.read_text()
for n in ['HAVE_ATEXIT','HAVE_VSNPRINTF','HAVE_VPRINTF']:s=s.replace('/* #undef '+n+' */','#define '+n+' 1')
p.write_text(s)
make(glib,'libglib.la','install-exec-am','install-data-am')
make(glib/'gmodule','libgmodule.la','install-exec-am','install-data-am')
env.update(GLIB_CONFIG=str(PREFIX/'bin/glib-config'),GTK_CONFIG=str(PREFIX/'bin/gtk-config'),
 LIBS='-lX11 -lxcb -lXau',CPPFLAGS='-I'+str(ROOT/'src/build/x11/prefix/include'),LDFLAGS='-L'+str(ROOT/'src/build/x11/prefix/lib'))
gtk=configure('gtk',SRC/'deps/gtk+-1.2.10',['--prefix='+str(PREFIX),'--disable-glibtest','--disable-nls','--disable-xim','--disable-shm','--with-x','--x-includes='+str(ROOT/'src/build/x11/prefix/include'),'--x-libraries='+str(ROOT/'src/build/x11/prefix/lib')])
make(gtk/'gdk','libgdk.la','install-exec-am','install-data-am')
make(gtk/'gtk','libgtk.la','install-exec-am','install-data-am')
make(gtk,'install-binSCRIPTS')
player=configure('player',SRC/'upstream',['--prefix=/usr','--disable-nls','--disable-glibtest','--disable-gtktest','--disable-alsatest','--disable-esd','--disable-alsa','--disable-vorbis','--disable-mikmod','--disable-oss','--disable-opengl','--disable-x86-asm'])
edit_config(player/'config.h')
for name in ['libxmms','Input/wav','Input/mpg123']:
 p=player/name/'config.h'
 if p.exists():p.write_text((player/'config.h').read_text())
 make(player/name,{'libxmms':'libxmms.la','Input/wav':'libwav.la','Input/mpg123':'libmpg123.la'}[name])
# Build objects only: upstream's final link expects dlopen, shared plugins and libm.
makefile=(player/'xmms/Makefile').read_text().replace('\\\n',' ')
objects=re.search(r'^am_xmms_OBJECTS = (.+)$',makefile,re.M)[1].replace('$(OBJEXT)','o').split()
make(player/'xmms',*objects)
run(['python3',SRC/'opensillicon/link.py'],ROOT,'src/build/xmms/link.log')
print('Built '+str(BUILD/'xmms'))
