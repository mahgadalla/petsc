#include <petsc/private/fortranimpl.h>
#include <petscsnes.h>
#include <petscviewer.h>
#include <petsc/private/f90impl.h>

#if defined(PETSC_HAVE_FORTRAN_CAPS)
#define matmffdcomputejacobian_          MATMFFDCOMPUTEJACOBIAN
#define snessolve_                       SNESSOLVE
#define snescomputejacobiandefault_      SNESCOMPUTEJACOBIANDEFAULT
#define snescomputejacobiandefaultcolor_ SNESCOMPUTEJACOBIANDEFAULTCOLOR
#define snessetjacobian_                 SNESSETJACOBIAN
#define snesgetoptionsprefix_            SNESGETOPTIONSPREFIX
#define snesgettype_                     SNESGETTYPE
#define snessetfunction_                 SNESSETFUNCTION
#define snessetngs_                       SNESSETNGS
#define snessetupdate_                    SNESSETUPDATE
#define snesgetfunction_                 SNESGETFUNCTION
#define snesgetngs_                       SNESGETNGS
#define snessetconvergencetest_          SNESSETCONVERGENCETEST
#define snesconvergeddefault_            SNESCONVERGEDDEFAULT
#define snesconvergedskip_               SNESCONVERGEDSKIP
#define snesview_                        SNESVIEW
#define snesgetconvergencehistory_       SNESGETCONVERGENCEHISTORY
#define snesgetjacobian_                 SNESGETJACOBIAN
#define snessettype_                     SNESSETTYPE
#define snesappendoptionsprefix_         SNESAPPENDOPTIONSPREFIX
#define snessetoptionsprefix_            SNESSETOPTIONSPREFIX
#define snesmonitordefault_              SNESMONITORDEFAULT
#define snesmonitorsolution_             SNESMONITORSOLUTION
#define snesmonitorlgresidualnorm_       SNESMONITORLGRESIDUALNORM
#define snesmonitorsolutionupdate_       SNESMONITORSOLUTIONUPDATE
#define snesmonitorset_                  SNESMONITORSET
#define snesnewtontrsetpostcheck_        SNESNEWTONTRSETPOSTCHECK
#elif !defined(PETSC_HAVE_FORTRAN_UNDERSCORE)
#define matmffdcomputejacobian_          matmffdcomputejacobian
#define snessolve_                       snessolve
#define snescomputejacobiandefault_      snescomputejacobiandefault
#define snescomputejacobiandefaultcolor_ snescomputejacobiandefaultcolor
#define snessetjacobian_                 snessetjacobian
#define snesgetoptionsprefix_            snesgetoptionsprefix
#define snesgettype_                     snesgettype
#define snessetfunction_                 snessetfunction
#define snessetngs_                       snessetngs
#define snessetupdate_                    snessetupdate
#define snesgetfunction_                 snesgetfunction
#define snesgetngs_                       snesgetngs
#define snessetconvergencetest_          snessetconvergencetest
#define snesconvergeddefault_            snesconvergeddefault
#define snesconvergedskip_               snesconvergedskip
#define snesview_                        snesview
#define snesgetjacobian_                 snesgetjacobian
#define snesgetconvergencehistory_       snesgetconvergencehistory
#define snessettype_                     snessettype
#define snesappendoptionsprefix_         snesappendoptionsprefix
#define snessetoptionsprefix_            snessetoptionsprefix
#define snesmonitorlgresidualnorm_       snesmonitorlgresidualnorm
#define snesmonitordefault_              snesmonitordefault
#define snesmonitorsolution_             snesmonitorsolution
#define snesmonitorsolutionupdate_       snesmonitorsolutionupdate
#define snesmonitorset_                  snesmonitorset
#define snesnewtontrsetpostcheck_        snesnewtontrsetpostcheck
#endif

static struct {
  PetscFortranCallbackId function;
  PetscFortranCallbackId test;
  PetscFortranCallbackId destroy;
  PetscFortranCallbackId jacobian;
  PetscFortranCallbackId monitor;
  PetscFortranCallbackId mondestroy;
  PetscFortranCallbackId ngs;
  PetscFortranCallbackId update;
  PetscFortranCallbackId trpostcheck;
#if defined(PETSC_HAVE_F90_2PTR_ARG)
  PetscFortranCallbackId function_pgiptr;
  PetscFortranCallbackId trpostcheck_pgiptr;
#endif
} _cb;

static PetscErrorCode ourtrpostcheckfunction(SNES snes,Vec x,Vec y,Vec w,PetscBool *changed_w,void *ctx)
{
#if defined(PETSC_HAVE_F90_2PTR_ARG)
  void* ptr;
  PetscObjectGetFortranCallback((PetscObject)snes,PETSC_FORTRAN_CALLBACK_CLASS,_cb.trpostcheck_pgiptr,NULL,&ptr);
#endif
  PetscObjectUseFortranCallback(snes,_cb.trpostcheck,(SNES*,Vec*,Vec*,Vec *,PetscBool *,void*,PetscErrorCode* PETSC_F90_2PTR_PROTO_NOVAR),(&snes,&x,&y,&w,changed_w,_ctx,&ierr PETSC_F90_2PTR_PARAM(ptr)));
}

PETSC_EXTERN void PETSC_STDCALL snesnewtontrsetpostcheck_(SNES *snes, void (PETSC_STDCALL *func)(SNES,Vec,Vec,Vec,PetscBool*,void*),void *ctx,PetscErrorCode *ierr PETSC_F90_2PTR_PROTO(ptr))
{
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.trpostcheck,(PetscVoidFunction)func,ctx);if (*ierr) return;
#if defined(PETSC_HAVE_F90_2PTR_ARG)
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.trpostcheck_pgiptr,NULL,ptr);if (*ierr) return;
#endif
  SNESNewtonTRSetPostCheck(*snes,ourtrpostcheckfunction,NULL);
}



static PetscErrorCode oursnesfunction(SNES snes,Vec x,Vec f,void *ctx)
{
#if defined(PETSC_HAVE_F90_2PTR_ARG)
  void* ptr;
  PetscObjectGetFortranCallback((PetscObject)snes,PETSC_FORTRAN_CALLBACK_CLASS,_cb.function_pgiptr,NULL,&ptr);
#endif
  PetscObjectUseFortranCallback(snes,_cb.function,(SNES*,Vec*,Vec*,void*,PetscErrorCode* PETSC_F90_2PTR_PROTO_NOVAR),(&snes,&x,&f,_ctx,&ierr PETSC_F90_2PTR_PARAM(ptr)));
}

static PetscErrorCode oursnestest(SNES snes,PetscInt it,PetscReal a,PetscReal d,PetscReal c,SNESConvergedReason *reason,void *ctx)
{
  PetscObjectUseFortranCallback(snes,_cb.test,(SNES*,PetscInt*,PetscReal*,PetscReal*,PetscReal*,SNESConvergedReason*,void*,PetscErrorCode*),(&snes,&it,&a,&d,&c,reason,_ctx,&ierr));
}

static PetscErrorCode ourdestroy(void *ctx)
{
  PetscObjectUseFortranCallback(ctx,_cb.destroy,(void*,PetscErrorCode*),(_ctx,&ierr));
}

static PetscErrorCode oursnesjacobian(SNES snes,Vec x,Mat m,Mat p,void *ctx)
{
  PetscObjectUseFortranCallback(snes,_cb.jacobian,(SNES*,Vec*,Mat*,Mat*,void*,PetscErrorCode*),(&snes,&x,&m,&p,_ctx,&ierr));
}

static PetscErrorCode oursnesupdate(SNES snes,PetscInt i)
{
  PetscObjectUseFortranCallback(snes,_cb.update,(SNES*,PetscInt *,PetscErrorCode*),(&snes,&i,&ierr));
}
static PetscErrorCode oursnesngs(SNES snes,Vec x,Vec b,void *ctx)
{
  PetscObjectUseFortranCallback(snes,_cb.ngs,(SNES*,Vec*,Vec*,void*,PetscErrorCode*),(&snes,&x,&b,_ctx,&ierr));
}
static PetscErrorCode oursnesmonitor(SNES snes,PetscInt i,PetscReal d,void *ctx)
{
  PetscObjectUseFortranCallback(snes,_cb.monitor,(SNES*,PetscInt*,PetscReal*,void*,PetscErrorCode*),(&snes,&i,&d,_ctx,&ierr));
}
static PetscErrorCode ourmondestroy(void **ctx)
{
  SNES snes = (SNES)*ctx;
  PetscObjectUseFortranCallback(snes,_cb.mondestroy,(void*,PetscErrorCode*),(_ctx,&ierr));
}

/* ---------------------------------------------------------*/
/*
     snescomputejacobiandefault() and snescomputejacobiandefaultcolor()
  These can be used directly from Fortran but are mostly so that
  Fortran SNESSetJacobian() will properly handle the defaults being passed in.

  functions, hence no STDCALL
*/
PETSC_EXTERN void matmffdcomputejacobian_(SNES *snes,Vec *x,Mat *m,Mat *p,void *ctx,PetscErrorCode *ierr)
{
  *ierr = MatMFFDComputeJacobian(*snes,*x,*m,*p,ctx);
}
PETSC_EXTERN void snescomputejacobiandefault_(SNES *snes,Vec *x,Mat *m,Mat *p,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SNESComputeJacobianDefault(*snes,*x,*m,*p,ctx);
}
PETSC_EXTERN void  snescomputejacobiandefaultcolor_(SNES *snes,Vec *x,Mat *m,Mat *p,void *ctx,PetscErrorCode *ierr)
{
  *ierr = SNESComputeJacobianDefaultColor(*snes,*x,*m,*p,*(MatFDColoring*)ctx);
}

PETSC_EXTERN void PETSC_STDCALL snessetjacobian_(SNES *snes,Mat *A,Mat *B,
                                    void (PETSC_STDCALL *func)(SNES*,Vec*,Mat*,Mat*,void*,PetscErrorCode*),
                                    void *ctx,PetscErrorCode *ierr)
{
  CHKFORTRANNULLFUNCTION(func);
  if ((PetscVoidFunction)func == (PetscVoidFunction)snescomputejacobiandefault_) {
    *ierr = SNESSetJacobian(*snes,*A,*B,SNESComputeJacobianDefault,ctx);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)snescomputejacobiandefaultcolor_) {
    if (!ctx) {
      *ierr = PETSC_ERR_ARG_NULL;
      return;
    }
    *ierr = SNESSetJacobian(*snes,*A,*B,SNESComputeJacobianDefaultColor,*(MatFDColoring*)ctx);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)matmffdcomputejacobian_) {
    *ierr = SNESSetJacobian(*snes,*A,*B,MatMFFDComputeJacobian,ctx);
  } else {
    *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.jacobian,(PetscVoidFunction)func,ctx);
    if (!*ierr) *ierr = SNESSetJacobian(*snes,*A,*B,oursnesjacobian,NULL);
  }
}
/* -------------------------------------------------------------*/

PETSC_EXTERN void PETSC_STDCALL snesgetoptionsprefix_(SNES *snes,char* prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  const char *tname;

  *ierr = SNESGetOptionsPrefix(*snes,&tname);
  *ierr = PetscStrncpy(prefix,tname,len);if (*ierr) return;
  FIXRETURNCHAR(PETSC_TRUE,prefix,len);
}

PETSC_EXTERN void PETSC_STDCALL snesgettype_(SNES *snes,char* name PETSC_MIXED_LEN(len), PetscErrorCode *ierr PETSC_END_LEN(len))
{
  const char *tname;

  *ierr = SNESGetType(*snes,&tname);
  *ierr = PetscStrncpy(name,tname,len);if (*ierr) return;
  FIXRETURNCHAR(PETSC_TRUE,name,len);
}

/* ---------------------------------------------------------*/

/*
   These are not usually called from Fortran but allow Fortran users
   to transparently set these monitors from .F code

   functions, hence no STDCALL
*/

PETSC_EXTERN void PETSC_STDCALL snessetfunction_(SNES *snes,Vec *r,void (PETSC_STDCALL *func)(SNES*,Vec*,Vec*,void*,PetscErrorCode*),void *ctx,PetscErrorCode *ierr PETSC_F90_2PTR_PROTO(ptr))
{
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.function,(PetscVoidFunction)func,ctx);if (*ierr) return;
#if defined(PETSC_HAVE_F90_2PTR_ARG)
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.function_pgiptr,NULL,ptr);if (*ierr) return;
#endif
  *ierr = SNESSetFunction(*snes,*r,oursnesfunction,NULL);
}


PETSC_EXTERN void PETSC_STDCALL snessetngs_(SNES *snes,void (PETSC_STDCALL *func)(SNES*,Vec*,Vec*,void*,PetscErrorCode*),void *ctx,PetscErrorCode *ierr)
{
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.ngs,(PetscVoidFunction)func,ctx);if (*ierr) return;
  *ierr = SNESSetNGS(*snes,oursnesngs,NULL);
}
PETSC_EXTERN void PETSC_STDCALL snessetupdate_(SNES *snes,void (PETSC_STDCALL *func)(SNES*,PetscInt*,PetscErrorCode*),PetscErrorCode *ierr)
{
  *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.update,(PetscVoidFunction)func,NULL);if (*ierr) return;
  *ierr = SNESSetUpdate(*snes,oursnesupdate);
}
/* ---------------------------------------------------------*/

/* the func argument is ignored */
PETSC_EXTERN void PETSC_STDCALL snesgetfunction_(SNES *snes,Vec *r,void (PETSC_STDCALL *func)(SNES,Vec,Vec,void*),void **ctx,PetscErrorCode *ierr)
{
  CHKFORTRANNULLOBJECT(r);
  *ierr = SNESGetFunction(*snes,r,NULL,NULL); if (*ierr) return;
  if ((PetscVoidFunction)func == (PetscVoidFunction)PETSC_NULL_FUNCTION_Fortran) return;
  *ierr = PetscObjectGetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,_cb.function,NULL,ctx);
}

PETSC_EXTERN void PETSC_STDCALL snesgetngs_(SNES *snes,void *func,void **ctx,PetscErrorCode *ierr)
{
  *ierr = PetscObjectGetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,_cb.ngs,NULL,ctx);
}

/*----------------------------------------------------------------------*/

PETSC_EXTERN void snesconvergeddefault_(SNES *snes,PetscInt *it,PetscReal *a,PetscReal *b,PetscReal *c,SNESConvergedReason *r, void *ct,PetscErrorCode *ierr)
{
  *ierr = SNESConvergedDefault(*snes,*it,*a,*b,*c,r,ct);
}

PETSC_EXTERN void snesconvergedskip_(SNES *snes,PetscInt *it,PetscReal *a,PetscReal *b,PetscReal *c,SNESConvergedReason *r,void *ct,PetscErrorCode *ierr)
{
  *ierr = SNESConvergedSkip(*snes,*it,*a,*b,*c,r,ct);
}

PETSC_EXTERN void PETSC_STDCALL snessetconvergencetest_(SNES *snes,void (PETSC_STDCALL *func)(SNES*,PetscInt*,PetscReal*,PetscReal*,PetscReal*,SNESConvergedReason*,void*,PetscErrorCode*), void *cctx,void (PETSC_STDCALL *destroy)(void*),PetscErrorCode *ierr)
{
  CHKFORTRANNULLFUNCTION(destroy);

  if ((PetscVoidFunction)func == (PetscVoidFunction)snesconvergeddefault_) {
    *ierr = SNESSetConvergenceTest(*snes,SNESConvergedDefault,0,0);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)snesconvergedskip_) {
    *ierr = SNESSetConvergenceTest(*snes,SNESConvergedSkip,0,0);
  } else {
    *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.test,(PetscVoidFunction)func,cctx);if (*ierr) return;
    *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.destroy,(PetscVoidFunction)destroy,cctx);if (*ierr) return;
    *ierr = SNESSetConvergenceTest(*snes,oursnestest,*snes,ourdestroy);
  }
}
/*----------------------------------------------------------------------*/

PETSC_EXTERN void PETSC_STDCALL snesview_(SNES *snes,PetscViewer *viewer, PetscErrorCode *ierr)
{
  PetscViewer v;
  PetscPatchDefaultViewers_Fortran(viewer,v);
  *ierr = SNESView(*snes,v);
}

/*  func is currently ignored from Fortran */
PETSC_EXTERN void PETSC_STDCALL snesgetjacobian_(SNES *snes,Mat *A,Mat *B,int *func,void **ctx,PetscErrorCode *ierr)
{
  CHKFORTRANNULLINTEGER(ctx);
  CHKFORTRANNULLOBJECT(A);
  CHKFORTRANNULLOBJECT(B);
  *ierr = SNESGetJacobian(*snes,A,B,0,NULL); if (*ierr) return;
  *ierr = PetscObjectGetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,_cb.jacobian,NULL,ctx);

}

PETSC_EXTERN void PETSC_STDCALL snesgetconvergencehistory_(SNES *snes,PetscInt *na,PetscErrorCode *ierr)
{
  *ierr = SNESGetConvergenceHistory(*snes,NULL,NULL,na);
}

PETSC_EXTERN void PETSC_STDCALL snessettype_(SNES *snes,char* type PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(type,len,t);
  *ierr = SNESSetType(*snes,t);if (*ierr) return;
  FREECHAR(type,t);
}

PETSC_EXTERN void PETSC_STDCALL snesappendoptionsprefix_(SNES *snes,char* prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(prefix,len,t);
  *ierr = SNESAppendOptionsPrefix(*snes,t);if (*ierr) return;
  FREECHAR(prefix,t);
}

PETSC_EXTERN void PETSC_STDCALL snessetoptionsprefix_(SNES *snes,char* prefix PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;

  FIXCHAR(prefix,len,t);
  *ierr = SNESSetOptionsPrefix(*snes,t);if (*ierr) return;
  FREECHAR(prefix,t);
}

/*----------------------------------------------------------------------*/
/* functions, hence no STDCALL */

PETSC_EXTERN void snesmonitorlgresidualnorm_(SNES *snes,PetscInt *its,PetscReal *fgnorm,PetscObject *dummy,PetscErrorCode *ierr)
{
  *ierr = SNESMonitorLGResidualNorm(*snes,*its,*fgnorm,dummy);
}

PETSC_EXTERN void snesmonitordefault_(SNES *snes,PetscInt *its,PetscReal *fgnorm,PetscViewerAndFormat **dummy,PetscErrorCode *ierr)
{
  *ierr = SNESMonitorDefault(*snes,*its,*fgnorm,*dummy);
}

PETSC_EXTERN void snesmonitorsolution_(SNES *snes,PetscInt *its,PetscReal *fgnorm,PetscViewerAndFormat **dummy,PetscErrorCode *ierr)
{
  *ierr = SNESMonitorSolution(*snes,*its,*fgnorm,*dummy);
}

PETSC_EXTERN void snesmonitorsolutionupdate_(SNES *snes,PetscInt *its,PetscReal *fgnorm,PetscViewerAndFormat **dummy,PetscErrorCode *ierr)
{
  *ierr = SNESMonitorSolutionUpdate(*snes,*its,*fgnorm,*dummy);
}


PETSC_EXTERN void PETSC_STDCALL snesmonitorset_(SNES *snes,void (PETSC_STDCALL *func)(SNES*,PetscInt*,PetscReal*,void*,PetscErrorCode*),void *mctx,void (PETSC_STDCALL *mondestroy)(void*,PetscErrorCode*),PetscErrorCode *ierr)
{
  CHKFORTRANNULLFUNCTION(mondestroy);
  if ((PetscVoidFunction)func == (PetscVoidFunction)snesmonitordefault_) {
    *ierr = SNESMonitorSet(*snes,(PetscErrorCode (*)(SNES,PetscInt,PetscReal,void*))SNESMonitorDefault,*(PetscViewerAndFormat**)mctx,(PetscErrorCode (*)(void **))PetscViewerAndFormatDestroy);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)snesmonitorsolution_) {
    *ierr = SNESMonitorSet(*snes,(PetscErrorCode (*)(SNES,PetscInt,PetscReal,void*))SNESMonitorSolution,*(PetscViewerAndFormat**)mctx,(PetscErrorCode (*)(void **))PetscViewerAndFormatDestroy);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)snesmonitorsolutionupdate_) {
    *ierr = SNESMonitorSet(*snes,(PetscErrorCode (*)(SNES,PetscInt,PetscReal,void*))SNESMonitorSolutionUpdate,*(PetscViewerAndFormat**)mctx,(PetscErrorCode (*)(void **))PetscViewerAndFormatDestroy);
  } else if ((PetscVoidFunction)func == (PetscVoidFunction)snesmonitorlgresidualnorm_) {
    *ierr = SNESMonitorSet(*snes,(PetscErrorCode (*)(SNES,PetscInt,PetscReal,void*))SNESMonitorLGResidualNorm,0,0);
  } else {
    *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.monitor,(PetscVoidFunction)func,mctx);if (*ierr) return;
    *ierr = PetscObjectSetFortranCallback((PetscObject)*snes,PETSC_FORTRAN_CALLBACK_CLASS,&_cb.mondestroy,(PetscVoidFunction)mondestroy,mctx);if (*ierr) return;
    *ierr = SNESMonitorSet(*snes,oursnesmonitor,*snes,ourmondestroy);
  }
}

