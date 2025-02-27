/*

   This file defines part of the initialization of PETSc

  This file uses regular malloc and free because it cannot known
  what malloc is being used until it has already processed the input.
*/

#include <petscsys.h>        /*I  "petscsys.h"   I*/
#include <petsc/private/petscimpl.h>
#include <petscvalgrind.h>
#include <petscviewer.h>
#if defined(PETSC_USE_LOG)
PETSC_INTERN PetscErrorCode PetscLogInitialize(void);
#endif

#if defined(PETSC_HAVE_SYS_SYSINFO_H)
#include <sys/sysinfo.h>
#endif
#if defined(PETSC_HAVE_UNISTD_H)
#include <unistd.h>
#endif
#if defined(PETSC_HAVE_CUDA)
#include <cuda_runtime.h>
extern PetscErrorCode PetscCUBLASInitializeHandle(void);
#endif

#if defined(PETSC_HAVE_VIENNACL)
PETSC_EXTERN PetscErrorCode PetscViennaCLInit();
#endif

/* ------------------------Nasty global variables -------------------------------*/
/*
     Indicates if PETSc started up MPI, or it was
   already started before PETSc was initialized.
*/
PetscBool   PetscBeganMPI         = PETSC_FALSE;
PetscBool   PetscInitializeCalled = PETSC_FALSE;
PetscBool   PetscFinalizeCalled   = PETSC_FALSE;
PetscBool   PetscCUDAInitialized  = PETSC_FALSE;

PetscMPIInt PetscGlobalRank       = -1;
PetscMPIInt PetscGlobalSize       = -1;

#if defined(PETSC_HAVE_COMPLEX)
#if defined(PETSC_COMPLEX_INSTANTIATE)
template <> class std::complex<double>; /* instantiate complex template class */
#endif
#if !defined(PETSC_HAVE_MPI_C_DOUBLE_COMPLEX)
MPI_Datatype MPIU_C_DOUBLE_COMPLEX;
MPI_Datatype MPIU_C_COMPLEX;
#endif

/*MC
   PETSC_i - the imaginary number i

   Synopsis:
   #include <petscsys.h>
   PetscComplex PETSC_i;

   Level: beginner

   Note:
   Complex numbers are automatically available if PETSc located a working complex implementation

.seealso: PetscRealPart(), PetscImaginaryPart(), PetscRealPartComplex(), PetscImaginaryPartComplex()
M*/
PetscComplex PETSC_i;
#endif
#if defined(PETSC_USE_REAL___FLOAT128)
MPI_Datatype MPIU___FLOAT128 = 0;
#if defined(PETSC_HAVE_COMPLEX)
MPI_Datatype MPIU___COMPLEX128 = 0;
#endif
#elif defined(PETSC_USE_REAL___FP16)
MPI_Datatype MPIU___FP16 = 0;
#endif
MPI_Datatype MPIU_2SCALAR = 0;
#if defined(PETSC_USE_64BIT_INDICES)
MPI_Datatype MPIU_2INT = 0;
#endif
MPI_Datatype MPIU_BOOL;
MPI_Datatype MPIU_ENUM;
MPI_Datatype MPIU_FORTRANADDR;
MPI_Datatype MPIU_SIZE_T;

/*
       Function that is called to display all error messages
*/
PetscErrorCode (*PetscErrorPrintf)(const char [],...)          = PetscErrorPrintfDefault;
PetscErrorCode (*PetscHelpPrintf)(MPI_Comm,const char [],...)  = PetscHelpPrintfDefault;
PetscErrorCode (*PetscVFPrintf)(FILE*,const char[],va_list)    = PetscVFPrintfDefault;
/*
  This is needed to turn on/off GPU synchronization
*/
PetscBool PetscViennaCLSynchronize = PETSC_FALSE;
PetscBool PetscCUDASynchronize = PETSC_FALSE;

/* ------------------------------------------------------------------------------*/
/*
   Optional file where all PETSc output from various prints is saved
*/
PETSC_INTERN FILE *petsc_history;
FILE *petsc_history = NULL;

PetscErrorCode  PetscOpenHistoryFile(const char filename[],FILE **fd)
{
  PetscErrorCode ierr;
  PetscMPIInt    rank,size;
  char           pfile[PETSC_MAX_PATH_LEN],pname[PETSC_MAX_PATH_LEN],fname[PETSC_MAX_PATH_LEN],date[64];
  char           version[256];

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  if (!rank) {
    char        arch[10];
    int         err;

    ierr = PetscGetArchType(arch,10);CHKERRQ(ierr);
    ierr = PetscGetDate(date,64);CHKERRQ(ierr);
    ierr = PetscGetVersion(version,256);CHKERRQ(ierr);
    ierr = MPI_Comm_size(PETSC_COMM_WORLD,&size);CHKERRQ(ierr);
    if (filename) {
      ierr = PetscFixFilename(filename,fname);CHKERRQ(ierr);
    } else {
      ierr = PetscGetHomeDirectory(pfile,240);CHKERRQ(ierr);
      ierr = PetscStrcat(pfile,"/.petschistory");CHKERRQ(ierr);
      ierr = PetscFixFilename(pfile,fname);CHKERRQ(ierr);
    }

    *fd = fopen(fname,"a");
    if (!fd) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_FILE_OPEN,"Cannot open file: %s",fname);

    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"---------------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"%s %s\n",version,date);CHKERRQ(ierr);
    ierr = PetscGetProgramName(pname,PETSC_MAX_PATH_LEN);CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"%s on a %s, %d proc. with options:\n",pname,arch,size);CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"---------------------------------------------------------\n");CHKERRQ(ierr);

    err = fflush(*fd);
    if (err) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SYS,"fflush() failed on file");
  }
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode PetscCloseHistoryFile(FILE **fd)
{
  PetscErrorCode ierr;
  PetscMPIInt    rank;
  char           date[64];
  int            err;

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(PETSC_COMM_WORLD,&rank);CHKERRQ(ierr);
  if (!rank) {
    ierr = PetscGetDate(date,64);CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"---------------------------------------------------------\n");CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"Finished at %s\n",date);CHKERRQ(ierr);
    ierr = PetscFPrintf(PETSC_COMM_SELF,*fd,"---------------------------------------------------------\n");CHKERRQ(ierr);
    err  = fflush(*fd);
    if (err) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SYS,"fflush() failed on file");
    err = fclose(*fd);
    if (err) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_SYS,"fclose() failed on file");
  }
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------------------*/

/*
   This is ugly and probably belongs somewhere else, but I want to
  be able to put a true MPI abort error handler with command line args.

    This is so MPI errors in the debugger will leave all the stack
  frames. The default MP_Abort() cleans up and exits thus providing no useful information
  in the debugger hence we call abort() instead of MPI_Abort().
*/

void Petsc_MPI_AbortOnError(MPI_Comm *comm,PetscMPIInt *flag,...)
{
  PetscFunctionBegin;
  (*PetscErrorPrintf)("MPI error %d\n",*flag);
  abort();
}

void Petsc_MPI_DebuggerOnError(MPI_Comm *comm,PetscMPIInt *flag,...)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  (*PetscErrorPrintf)("MPI error %d\n",*flag);
  ierr = PetscAttachDebugger();
  if (ierr) MPI_Abort(*comm,*flag); /* hopeless so get out */
}

#if defined(PETSC_HAVE_CUDA)
/*@C
     PetscCUDAInitialize - Initializes the CUDA device and cuBLAS on the device

     Logically collective

  Input Parameter:
  comm - the MPI communicator that will utilize the CUDA devices

  Options Database:
+  -cuda_initialize - do the initialization in PetscInitialize()
.  -cuda_view - view information about the CUDA devices
.  -cuda_synchronize - wait at the end of asynchronize CUDA calls so that their time gets credited to the current event; default with -log_view
-  -cuda_set_device <gpu> - integer number of the device

  Notes:
   Initializing cuBLAS takes about 1/2 second there it is done by default in PetscInitialize() before logging begins

@*/
PetscErrorCode PetscCUDAInitialize(MPI_Comm comm)
{
  PetscErrorCode        ierr;
  PetscInt              deviceOpt = 0;
  PetscBool             cuda_view_flag = PETSC_FALSE,flg;
  struct cudaDeviceProp prop;
  int                   devCount,device,devicecnt;
  cudaError_t           err = cudaSuccess;
  PetscMPIInt           rank,size;

  PetscFunctionBegin;
  /*
     If collecting logging information, by default, wait for GPU to complete its operations
     before returning to the CPU in order to get accurate timings of each event
  */
  ierr = PetscOptionsHasName(NULL,NULL,"-log_summary",&PetscCUDASynchronize);CHKERRQ(ierr);
  if (!PetscCUDASynchronize) {
    ierr = PetscOptionsHasName(NULL,NULL,"-log_view",&PetscCUDASynchronize);CHKERRQ(ierr);
  }

  ierr = PetscOptionsBegin(comm,NULL,"CUDA options","Sys");CHKERRQ(ierr);
  ierr = PetscOptionsInt("-cuda_set_device","Set all MPI ranks to use the specified CUDA device",NULL,deviceOpt,&deviceOpt,&flg);CHKERRQ(ierr);
  device = (int)deviceOpt;
  ierr = PetscOptionsBool("-cuda_synchronize","Wait for the GPU to complete operations before returning to the CPU",NULL,PetscCUDASynchronize,&PetscCUDASynchronize,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsDeprecated("-cuda_show_devices","-cuda_view","3.12",NULL);CHKERRQ(ierr);
  ierr = PetscOptionsName("-cuda_view","Display CUDA device information and assignments",NULL,&cuda_view_flag);CHKERRQ(ierr);
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  if (!PetscCUDAInitialized) {
    ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);

    if (size>1 && !flg) {
      /* check to see if we force multiple ranks to hit the same GPU */
      /* we're not using the same GPU on multiple MPI threads. So try to allocated different   GPUs to different processes */

      /* First get the device count */
      err   = cudaGetDeviceCount(&devCount);
      if (err != cudaSuccess) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"error in cudaGetDeviceCount %s",cudaGetErrorString(err));

      /* next determine the rank and then set the device via a mod */
      ierr   = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
      device = rank % devCount;
    }
    err = cudaSetDevice(device);
    if (err != cudaSuccess) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"error in cudaSetDevice %s",cudaGetErrorString(err));

    /* set the device flags so that it can map host memory */
    err = cudaSetDeviceFlags(cudaDeviceMapHost);
    if (err != cudaSuccess) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"error in cudaSetDeviceFlags %s",cudaGetErrorString(err));

    ierr = PetscCUBLASInitializeHandle();CHKERRQ(ierr);
    PetscCUDAInitialized = PETSC_TRUE;
  }
  if (cuda_view_flag) {
    ierr   = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
    err = cudaGetDeviceCount(&devCount);
    if (err != cudaSuccess) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"error in cudaGetDeviceCount %s",cudaGetErrorString(err));
    for (devicecnt = 0; devicecnt < devCount; ++devicecnt) {
      err = cudaGetDeviceProperties(&prop,devicecnt);
      if (err != cudaSuccess) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_SYS,"error in cudaGetDeviceProperties %s",cudaGetErrorString(err));
      ierr = PetscPrintf(comm, "CUDA device %d: %s\n", devicecnt, prop.name);CHKERRQ(ierr);
    }
    ierr = PetscSynchronizedPrintf(comm,"[%d] Using CUDA device %d.\n",rank,device);CHKERRQ(ierr);
    ierr = PetscSynchronizedFlush(comm,PETSC_STDOUT);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}
#endif

/*@C
   PetscEnd - Calls PetscFinalize() and then ends the program. This is useful if one
     wishes a clean exit somewhere deep in the program.

   Collective on PETSC_COMM_WORLD

   Options Database Keys are the same as for PetscFinalize()

   Level: advanced

   Note:
   See PetscInitialize() for more general runtime options.

.seealso: PetscInitialize(), PetscOptionsView(), PetscMallocDump(), PetscMPIDump(), PetscFinalize()
@*/
PetscErrorCode  PetscEnd(void)
{
  PetscFunctionBegin;
  PetscFinalize();
  exit(0);
  return 0;
}

PetscBool PetscOptionsPublish = PETSC_FALSE;
PETSC_INTERN PetscErrorCode PetscSetUseTrMalloc_Private(void);
PETSC_INTERN PetscErrorCode PetscSetUseHBWMalloc_Private(void);
PETSC_INTERN PetscBool      petscsetmallocvisited;
static       char           emacsmachinename[256];

PetscErrorCode (*PetscExternalVersionFunction)(MPI_Comm) = 0;
PetscErrorCode (*PetscExternalHelpFunction)(MPI_Comm)    = 0;

/*@C
   PetscSetHelpVersionFunctions - Sets functions that print help and version information
   before the PETSc help and version information is printed. Must call BEFORE PetscInitialize().
   This routine enables a "higher-level" package that uses PETSc to print its messages first.

   Input Parameter:
+  help - the help function (may be NULL)
-  version - the version function (may be NULL)

   Level: developer

@*/
PetscErrorCode  PetscSetHelpVersionFunctions(PetscErrorCode (*help)(MPI_Comm),PetscErrorCode (*version)(MPI_Comm))
{
  PetscFunctionBegin;
  PetscExternalHelpFunction    = help;
  PetscExternalVersionFunction = version;
  PetscFunctionReturn(0);
}

#if defined(PETSC_USE_LOG)
PETSC_INTERN PetscBool   PetscObjectsLog;
#endif

PETSC_INTERN PetscErrorCode  PetscOptionsCheckInitial_Private(void)
{
  char              string[64],mname[PETSC_MAX_PATH_LEN],*f;
  MPI_Comm          comm = PETSC_COMM_WORLD;
  PetscBool         flg1 = PETSC_FALSE,flg2 = PETSC_FALSE,flg3 = PETSC_FALSE,flag;
  PetscErrorCode    ierr;
  PetscReal         si;
  PetscInt          intensity;
  int               i;
  PetscMPIInt       rank;
  char              version[256],helpoptions[256];
#if !defined(PETSC_HAVE_THREADSAFETY)
  PetscReal         logthreshold;
#endif
#if defined(PETSC_USE_LOG)
  PetscViewerFormat format;
  PetscBool         flg4 = PETSC_FALSE;
#endif
#if defined(PETSC_HAVE_CUDA)
  PetscBool         initCuda = PETSC_TRUE;
#endif

  PetscFunctionBegin;
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);

#if !defined(PETSC_HAVE_THREADSAFETY)
  /*
      Setup the memory management; support for tracing malloc() usage
  */
  ierr = PetscOptionsHasName(NULL,NULL,"-malloc_log",&flg3);CHKERRQ(ierr);
  logthreshold = 0.0;
  ierr = PetscOptionsGetReal(NULL,NULL,"-malloc_log_threshold",&logthreshold,&flg1);CHKERRQ(ierr);
  if (flg1) flg3 = PETSC_TRUE;
#if defined(PETSC_USE_DEBUG)
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc",&flg1,&flg2);CHKERRQ(ierr);
  if ((!flg2 || flg1) && !petscsetmallocvisited) {
    if (flg2 || !(PETSC_RUNNING_ON_VALGRIND)) {
      /* turn off default -malloc if valgrind is being used */
      ierr = PetscSetUseTrMalloc_Private();CHKERRQ(ierr);
    }
  }
#else
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_dump",&flg1,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc",&flg2,NULL);CHKERRQ(ierr);
  if (flg1 || flg2 || flg3) {ierr = PetscSetUseTrMalloc_Private();CHKERRQ(ierr);}
#endif
  if (flg3) {
    ierr = PetscMallocSetDumpLogThreshold((PetscLogDouble)logthreshold);CHKERRQ(ierr);
  }
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_coalesce",&flg1,&flg2);CHKERRQ(ierr);
  if (flg2) {ierr = PetscMallocSetCoalesce(flg1);CHKERRQ(ierr);}
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_debug",&flg1,NULL);CHKERRQ(ierr);
  if (flg1) {
    ierr = PetscSetUseTrMalloc_Private();CHKERRQ(ierr);
    ierr = PetscMallocDebug(PETSC_TRUE);CHKERRQ(ierr);
  }
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_test",&flg1,NULL);CHKERRQ(ierr);
#if defined(PETSC_USE_DEBUG)
  if (flg1 && !PETSC_RUNNING_ON_VALGRIND) {
    ierr = PetscSetUseTrMalloc_Private();CHKERRQ(ierr);
    ierr = PetscMallocSetDumpLog();CHKERRQ(ierr);
    ierr = PetscMallocDebug(PETSC_TRUE);CHKERRQ(ierr);
  }
#endif
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_hbw",&flg1,NULL);CHKERRQ(ierr);
  /* ignore this option if malloc is already set */
  if (flg1 && !petscsetmallocvisited) {ierr = PetscSetUseHBWMalloc_Private();CHKERRQ(ierr);}

  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-malloc_info",&flg1,NULL);CHKERRQ(ierr);
  if (!flg1) {
    flg1 = PETSC_FALSE;
    ierr = PetscOptionsGetBool(NULL,NULL,"-memory_view",&flg1,NULL);CHKERRQ(ierr);
  }
  if (flg1) {
    ierr = PetscMemorySetGetMaximumUsage();CHKERRQ(ierr);
  }
#endif

#if defined(PETSC_USE_LOG)
  ierr = PetscOptionsHasName(NULL,NULL,"-objects_dump",&PetscObjectsLog);CHKERRQ(ierr);
#endif

  /*
      Set the display variable for graphics
  */
  ierr = PetscSetDisplay();CHKERRQ(ierr);

  /*
      Print the PETSc version information
  */
  ierr = PetscOptionsHasName(NULL,NULL,"-v",&flg1);CHKERRQ(ierr);
  ierr = PetscOptionsHasName(NULL,NULL,"-version",&flg2);CHKERRQ(ierr);
  ierr = PetscOptionsHasHelp(NULL,&flg3);CHKERRQ(ierr);
  if (flg1 || flg2 || flg3) {

    /*
       Print "higher-level" package version message
    */
    if (PetscExternalVersionFunction) {
      ierr = (*PetscExternalVersionFunction)(comm);CHKERRQ(ierr);
    }

    ierr = PetscGetVersion(version,256);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"--------------------------------------------------------------------------\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"%s\n",version);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"%s",PETSC_AUTHOR_INFO);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"See docs/changes/index.html for recent updates.\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"See docs/faq.html for problems.\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"See docs/manualpages/index.html for help. \n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"Libraries linked from %s\n",PETSC_LIB_DIR);CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"--------------------------------------------------------------------------\n");CHKERRQ(ierr);
  }

  /*
       Print "higher-level" package help message
  */
  if (flg3) {
    if (PetscExternalHelpFunction) {
      ierr = (*PetscExternalHelpFunction)(comm);CHKERRQ(ierr);
    }
  }

  ierr = PetscOptionsGetString(NULL,NULL,"-help",helpoptions,sizeof(helpoptions),&flg1);CHKERRQ(ierr);
  if (flg1) {
    ierr = PetscStrcmp(helpoptions,"intro",&flg2);CHKERRQ(ierr);
    if (flg2) {
      ierr = PetscOptionsDestroyDefault();CHKERRQ(ierr);
      ierr = PetscFreeMPIResources();CHKERRQ(ierr);
      ierr = MPI_Finalize();CHKERRQ(ierr);
      exit(0);
    }
  }

  /*
      Setup the error handling
  */
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-on_error_abort",&flg1,NULL);CHKERRQ(ierr);
  if (flg1) {
    ierr = MPI_Comm_set_errhandler(comm,MPI_ERRORS_ARE_FATAL);CHKERRQ(ierr);
    ierr = PetscPushErrorHandler(PetscAbortErrorHandler,0);CHKERRQ(ierr);
  }
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-on_error_mpiabort",&flg1,NULL);CHKERRQ(ierr);
  if (flg1) { ierr = PetscPushErrorHandler(PetscMPIAbortErrorHandler,0);CHKERRQ(ierr);}
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-mpi_return_on_error",&flg1,NULL);CHKERRQ(ierr);
  if (flg1) {
    ierr = MPI_Comm_set_errhandler(comm,MPI_ERRORS_RETURN);CHKERRQ(ierr);
  }
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-no_signal_handler",&flg1,NULL);CHKERRQ(ierr);
  if (!flg1) {ierr = PetscPushSignalHandler(PetscSignalHandlerDefault,(void*)0);CHKERRQ(ierr);}
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-fp_trap",&flg1,NULL);CHKERRQ(ierr);
  if (flg1) {ierr = PetscSetFPTrap(PETSC_FP_TRAP_ON);CHKERRQ(ierr);}
  ierr = PetscOptionsGetInt(NULL,NULL,"-check_pointer_intensity",&intensity,&flag);CHKERRQ(ierr);
  if (flag) {ierr = PetscCheckPointerSetIntensity(intensity);CHKERRQ(ierr);}

  /*
      Setup debugger information
  */
  ierr = PetscSetDefaultDebugger();CHKERRQ(ierr);
  ierr = PetscOptionsGetString(NULL,NULL,"-on_error_attach_debugger",string,64,&flg1);CHKERRQ(ierr);
  if (flg1) {
    MPI_Errhandler err_handler;

    ierr = PetscSetDebuggerFromString(string);CHKERRQ(ierr);
    ierr = MPI_Comm_create_errhandler(Petsc_MPI_DebuggerOnError,&err_handler);CHKERRQ(ierr);
    ierr = MPI_Comm_set_errhandler(comm,err_handler);CHKERRQ(ierr);
    ierr = PetscPushErrorHandler(PetscAttachDebuggerErrorHandler,0);CHKERRQ(ierr);
  }
  ierr = PetscOptionsGetString(NULL,NULL,"-debug_terminal",string,64,&flg1);CHKERRQ(ierr);
  if (flg1) { ierr = PetscSetDebugTerminal(string);CHKERRQ(ierr); }
  ierr = PetscOptionsGetString(NULL,NULL,"-start_in_debugger",string,64,&flg1);CHKERRQ(ierr);
  ierr = PetscOptionsGetString(NULL,NULL,"-stop_for_debugger",string,64,&flg2);CHKERRQ(ierr);
  if (flg1 || flg2) {
    PetscMPIInt    size;
    PetscInt       lsize,*nodes;
    MPI_Errhandler err_handler;
    /*
       we have to make sure that all processors have opened
       connections to all other processors, otherwise once the
       debugger has stated it is likely to receive a SIGUSR1
       and kill the program.
    */
    ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);
    if (size > 2) {
      PetscMPIInt dummy = 0;
      MPI_Status  status;
      for (i=0; i<size; i++) {
        if (rank != i) {
          ierr = MPI_Send(&dummy,1,MPI_INT,i,109,comm);CHKERRQ(ierr);
        }
      }
      for (i=0; i<size; i++) {
        if (rank != i) {
          ierr = MPI_Recv(&dummy,1,MPI_INT,i,109,comm,&status);CHKERRQ(ierr);
        }
      }
    }
    /* check if this processor node should be in debugger */
    ierr  = PetscMalloc1(size,&nodes);CHKERRQ(ierr);
    lsize = size;
    ierr  = PetscOptionsGetIntArray(NULL,NULL,"-debugger_nodes",nodes,&lsize,&flag);CHKERRQ(ierr);
    if (flag) {
      for (i=0; i<lsize; i++) {
        if (nodes[i] == rank) { flag = PETSC_FALSE; break; }
      }
    }
    if (!flag) {
      ierr = PetscSetDebuggerFromString(string);CHKERRQ(ierr);
      ierr = PetscPushErrorHandler(PetscAbortErrorHandler,0);CHKERRQ(ierr);
      if (flg1) {
        ierr = PetscAttachDebugger();CHKERRQ(ierr);
      } else {
        ierr = PetscStopForDebugger();CHKERRQ(ierr);
      }
      ierr = MPI_Comm_create_errhandler(Petsc_MPI_AbortOnError,&err_handler);CHKERRQ(ierr);
      ierr = MPI_Comm_set_errhandler(comm,err_handler);CHKERRQ(ierr);
    }
    ierr = PetscFree(nodes);CHKERRQ(ierr);
  }

  ierr = PetscOptionsGetString(NULL,NULL,"-on_error_emacs",emacsmachinename,128,&flg1);CHKERRQ(ierr);
  if (flg1 && !rank) {ierr = PetscPushErrorHandler(PetscEmacsClientErrorHandler,emacsmachinename);CHKERRQ(ierr);}

  /*
        Setup profiling and logging
  */
#if defined(PETSC_USE_INFO)
  {
    char logname[PETSC_MAX_PATH_LEN]; logname[0] = 0;
    ierr = PetscOptionsGetString(NULL,NULL,"-info",logname,250,&flg1);CHKERRQ(ierr);
    if (flg1 && logname[0]) {
      ierr = PetscInfoAllow(PETSC_TRUE,logname);CHKERRQ(ierr);
    } else if (flg1) {
      ierr = PetscInfoAllow(PETSC_TRUE,NULL);CHKERRQ(ierr);
    }
  }
#endif
#if defined(PETSC_USE_LOG)
  mname[0] = 0;
  ierr = PetscOptionsGetString(NULL,NULL,"-history",mname,PETSC_MAX_PATH_LEN,&flg1);CHKERRQ(ierr);
  if (flg1) {
    if (mname[0]) {
      ierr = PetscOpenHistoryFile(mname,&petsc_history);CHKERRQ(ierr);
    } else {
      ierr = PetscOpenHistoryFile(NULL,&petsc_history);CHKERRQ(ierr);
    }
  }

  ierr = PetscOptionsGetBool(NULL,NULL,"-log_sync",&PetscLogSyncOn,NULL);CHKERRQ(ierr);

#if defined(PETSC_HAVE_MPE)
  flg1 = PETSC_FALSE;
  ierr = PetscOptionsHasName(NULL,NULL,"-log_mpe",&flg1);CHKERRQ(ierr);
  if (flg1) {ierr = PetscLogMPEBegin();CHKERRQ(ierr);}
#endif
  flg1 = PETSC_FALSE;
  flg3 = PETSC_FALSE;
  ierr = PetscOptionsGetBool(NULL,NULL,"-log_all",&flg1,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsHasName(NULL,NULL,"-log_summary",&flg3);CHKERRQ(ierr);
  if (flg1)                      { ierr = PetscLogAllBegin();CHKERRQ(ierr); }
  else if (flg3)                 { ierr = PetscLogDefaultBegin();CHKERRQ(ierr);}

  ierr = PetscOptionsGetString(NULL,NULL,"-log_trace",mname,250,&flg1);CHKERRQ(ierr);
  if (flg1) {
    char name[PETSC_MAX_PATH_LEN],fname[PETSC_MAX_PATH_LEN];
    FILE *file;
    if (mname[0]) {
      PetscSNPrintf(name,PETSC_MAX_PATH_LEN,"%s.%d",mname,rank);
      ierr = PetscFixFilename(name,fname);CHKERRQ(ierr);
      file = fopen(fname,"w");
      if (!file) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_FILE_OPEN,"Unable to open trace file: %s",fname);
    } else file = PETSC_STDOUT;
    ierr = PetscLogTraceBegin(file);CHKERRQ(ierr);
  }

  ierr = PetscOptionsGetViewer(comm,NULL,NULL,"-log_view",NULL,&format,&flg4);CHKERRQ(ierr);
  if (flg4) {
    if (format == PETSC_VIEWER_ASCII_XML) {
      ierr = PetscLogNestedBegin();CHKERRQ(ierr);
    } else {
      ierr = PetscLogDefaultBegin();CHKERRQ(ierr);
    }
    PetscLogMemory = PETSC_FALSE;
    ierr = PetscOptionsGetBool(NULL,NULL,"-log_view_memory",&PetscLogMemory,NULL);CHKERRQ(ierr);
    if (PetscLogMemory) {
      ierr = PetscSetUseTrMalloc_Private();CHKERRQ(ierr);
    }
  }
  if (flg4 && format == PETSC_VIEWER_ASCII_XML) {
    PetscReal threshold = PetscRealConstant(0.01);
    ierr = PetscOptionsGetReal(NULL,NULL,"-log_threshold",&threshold,&flg1);CHKERRQ(ierr);
    if (flg1) {ierr = PetscLogSetThreshold((PetscLogDouble)threshold,NULL);CHKERRQ(ierr);}
  }
#endif

  ierr = PetscOptionsGetBool(NULL,NULL,"-saws_options",&PetscOptionsPublish,NULL);CHKERRQ(ierr);

#if defined(PETSC_HAVE_CUDA)
  ierr = PetscOptionsBegin(comm,NULL,"CUDA initialize","Sys");CHKERRQ(ierr);
  ierr = PetscOptionsBool("-cuda_initialize","Initialize the CUDA devices and cuBLAS during PetscInitialize()",NULL,initCuda,&initCuda,NULL);CHKERRQ(ierr);
  ierr = PetscOptionsEnd();CHKERRQ(ierr);
  if (initCuda) {ierr = PetscCUDAInitialize(PETSC_COMM_WORLD);CHKERRQ(ierr);}
#endif

  /*
       Print basic help message
  */
  ierr = PetscOptionsHasHelp(NULL,&flg1);CHKERRQ(ierr);
  if (flg1) {
    ierr = (*PetscHelpPrintf)(comm,"Options for all PETSc programs:\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -help: prints help method for each option\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -on_error_abort: cause an abort when an error is detected. Useful \n ");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"       only when run in the debugger\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -on_error_attach_debugger [gdb,dbx,xxgdb,ups,noxterm]\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"       start the debugger in new xterm\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"       unless noxterm is given\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -start_in_debugger [gdb,dbx,xxgdb,ups,noxterm]\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"       start all processes in the debugger\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -on_error_emacs <machinename>\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"    emacs jumps to error file\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -debugger_nodes [n1,n2,..] Nodes to start in debugger\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -debugger_pause [m] : delay (in seconds) to attach debugger\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -stop_for_debugger : prints message on how to attach debugger manually\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"                      waits the delay for you to attach\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -display display: Location where X window graphics and debuggers are displayed\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -no_signal_handler: do not trap error signals\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -mpi_return_on_error: MPI returns error code, rather than abort on internal error\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -fp_trap: stop on floating point exceptions\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"           note on IBM RS6000 this slows run greatly\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc_dump <optional filename>: dump list of unfreed memory at conclusion\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc: use our error checking malloc\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc no: don't use error checking malloc\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc_info: prints total memory usage\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc_log: keeps log of all memory allocations\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -malloc_debug: enables extended checking for memory corruption\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -options_view: dump list of options inputted\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -options_left: dump list of unused options\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -options_left no: don't dump list of unused options\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -tmp tmpdir: alternative /tmp directory\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -shared_tmp: tmp directory is shared by all processors\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -not_shared_tmp: each processor has separate tmp directory\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -memory_view: print memory usage at end of run\n");CHKERRQ(ierr);
#if defined(PETSC_USE_LOG)
    ierr = (*PetscHelpPrintf)(comm," -get_total_flops: total flops over all processors\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -log_view [:filename:[format]]: logging objects and events\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -log_trace [filename]: prints trace of all PETSc calls\n");CHKERRQ(ierr);
#if defined(PETSC_HAVE_MPE)
    ierr = (*PetscHelpPrintf)(comm," -log_mpe: Also create logfile viewable through Jumpshot\n");CHKERRQ(ierr);
#endif
    ierr = (*PetscHelpPrintf)(comm," -info <optional filename>: print informative messages about the calculations\n");CHKERRQ(ierr);
#endif
    ierr = (*PetscHelpPrintf)(comm," -v: prints PETSc version number and release date\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -options_file <file>: reads options from file\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm," -petsc_sleep n: sleeps n seconds before running program\n");CHKERRQ(ierr);
    ierr = (*PetscHelpPrintf)(comm,"-----------------------------------------------\n");CHKERRQ(ierr);
  }

#if defined(PETSC_HAVE_POPEN)
  {
  char machine[128];
  ierr = PetscOptionsGetString(NULL,NULL,"-popen_machine",machine,128,&flg1);CHKERRQ(ierr);
  if (flg1) {
    ierr = PetscPOpenSetMachine(machine);CHKERRQ(ierr);
  }
  }
#endif

  ierr = PetscOptionsGetReal(NULL,NULL,"-petsc_sleep",&si,&flg1);CHKERRQ(ierr);
  if (flg1) {
    ierr = PetscSleep(si);CHKERRQ(ierr);
  }

  ierr = PetscOptionsGetString(NULL,NULL,"-info_exclude",mname,PETSC_MAX_PATH_LEN,&flg1);CHKERRQ(ierr);
  if (flg1) {
    ierr = PetscStrstr(mname,"null",&f);CHKERRQ(ierr);
    if (f) {
      ierr = PetscInfoDeactivateClass(0);CHKERRQ(ierr);
    }
  }

#if defined(PETSC_HAVE_VIENNACL)
  ierr = PetscOptionsHasName(NULL,NULL,"-log_summary",&flg3);CHKERRQ(ierr);
  if (!flg3) {
    ierr = PetscOptionsHasName(NULL,NULL,"-log_view",&flg3);CHKERRQ(ierr);
  }
  ierr = PetscOptionsGetBool(NULL,NULL,"-viennacl_synchronize",&flg3,NULL);CHKERRQ(ierr);
  PetscViennaCLSynchronize = flg3;
  ierr = PetscViennaCLInit();CHKERRQ(ierr);
#endif

  /*
     Creates the logging data structures; this is enabled even if logging is not turned on
     This is the last thing we do before returning to the user code to prevent having the
     logging numbers contaminated by any startup time associated with MPI and the GPUs
  */
#if defined(PETSC_USE_LOG)
  ierr = PetscLogInitialize();CHKERRQ(ierr);
#endif

  PetscFunctionReturn(0);
}
