import config.package

#    We do not use CMAKE for OpenBLAS the cmake for OpenBLAS
#       does not have an install rule https://github.com/xianyi/OpenBLAS/issues/957
#       fails on mac due to argument list too long https://github.com/xianyi/OpenBLAS/issues/977
#       does not support 64 bit integers with INTERFACE64


class Configure(config.package.Package):
  def __init__(self, framework):
    config.package.Package.__init__(self, framework)
    self.version                = '0.3.6'
    self.gitcommit              = 'v'+self.version
    self.versionname            = 'OPENBLAS_VERSION'
    self.download               = ['git://https://github.com/xianyi/OpenBLAS.git','https://github.com/xianyi/OpenBLAS/archive/'+self.gitcommit+'.tar.gz']
    self.includes               = ['openblas_config.h']
    self.precisions             = ['single','double']
    self.skippackagewithoptions = 1
    self.fc                     = 1
    self.installwithbatch       = 1
    self.usespthreads           = 0

  def __str__(self):
    output  = config.package.Package.__str__(self)
    if self.usespthreads: output += '  using pthreads; use export OPENBLAS_NUM_THREADS=<p> to control the number of threads\n'
    return output

  def setupHelp(self, help):
    config.package.Package.setupHelp(self,help)
    import nargs
    help.addArgument('OpenBLAS', '-download-openblas-64-bit-blas-indices', nargs.ArgBool(None, 0, 'Use 64 bit integers for OpenBLAS'))
    help.addArgument('OpenBLAS', '-download-openblas-use-pthreads', nargs.ArgBool(None, 0, 'Use pthreads for OpenBLAS'))
    help.addArgument('OpenBLAS', '-download-openblas-make-options=<options>', nargs.Arg(None, None, 'additional options for building OpenBLAS'))
    return

  def setupDependencies(self, framework):
    config.package.Package.setupDependencies(self, framework)
    self.make            = framework.require('config.packages.make',self)
    self.openmp          = framework.require('config.packages.openmp',self)

  def configureLibrary(self):
    import os
    config.package.Package.configureLibrary(self)
    self.checkVersion()
    if self.found:
      self.libDir = os.path.join(self.directory,'lib')
    return

  def versionToStandardForm(self,ver):
    '''Converts from " OpenBLAS 0.3.6 " to standard 0.3.6 format'''
    import re
    return re.match("\s*OpenBLAS\s*([0-9\.]+)\s*",ver).group(1)

  def Install(self):
    import os

    # OpenBLAS handles its own compiler optimization options
    cmdline = 'CC='+self.compilers.CC+' '
    cmdline += 'FC='+self.compilers.FC+' '
    if self.argDB['download-openblas-64-bit-blas-indices'] or self.argDB['with-64-bit-blas-indices']:
      cmdline += " INTERFACE64=1 "
    if 'download-openblas-make-options' in self.argDB and self.argDB['download-openblas-make-options']:
      cmdline+=" "+self.argDB['download-openblas-make-options']
    if not self.argDB['with-shared-libraries']:
      cmdline += " NO_SHARED=1 "
    cmdline += " MAKE_NB_JOBS="+str(self.make.make_np)+" "
    if self.openmp.found:
      cmdline += " USE_OPENMP=1 "
      self.usesopenmp = 'yes'
      # use the environmental variable OMP_NUM_THREADS to control the number of threads used
    else:
      cmdline += " USE_OPENMP=0 "
      if 'download-openblas-use-pthreads' in self.argDB and self.argDB['download-openblas-use-pthreads']:
        self.usespthreads = 1
        cmdline += " USE_THREADS=1 "
        # use the environmental variable OPENBLAS_NUM_THREADS to control the number of threads used
      else:
        cmdline += " USE_THREADS=0 "
    cmdline += " NO_EXPRECISION=1 "

    libdir = self.libDir
    blasDir = self.packageDir

    g = open(os.path.join(blasDir,'tmpmakefile'),'w')
    g.write(cmdline)
    g.close()
    if not self.installNeeded('tmpmakefile'): return self.installDir

    try:
      self.logPrintBox('Compiling OpenBLAS; this may take several minutes')
      output1,err1,ret  = config.package.Package.executeShellCommand('cd '+blasDir+' && make '+cmdline, timeout=2500, log = self.log)
    except RuntimeError as e:
      raise RuntimeError('Error running make on '+blasDir+': '+str(e))
    try:
      self.logPrintBox('Installing OpenBLAS')
      self.installDirProvider.printSudoPasswordMessage()
      output2,err2,ret  = config.package.Package.executeShellCommand('cd '+blasDir+' && '+self.installSudo+' make PREFIX='+self.installDir+' install', timeout=30, log = self.log)
    except RuntimeError as e:
      raise RuntimeError('Error moving '+blasDir+' libraries: '+str(e))
    self.postInstall(output1+err1+output2+err2,'tmpmakefile')
    return self.installDir


