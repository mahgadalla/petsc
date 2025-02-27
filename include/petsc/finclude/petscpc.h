!
!
!  Include file for Fortran use of the PC (preconditioner) package in PETSc
!
#if !defined (PETSCPCDEF_H)
#define PETSCPCDEF_H

#include "petsc/finclude/petscmat.h"
#include "petsc/finclude/petscdm.h"

#define PC type(tPC)

#define PCSide PetscEnum
#define PCJacobiType PetscEnum
#define PCASMType PetscEnum
#define PCGASMType PetscEnum
#define PCCompositeType PetscEnum
#define PCRichardsonConvergedReason PetscEnum
#define PCType character*(80)
#define PCFieldSplitSchurPreType PetscEnum
#define PCPARMSGlobalType PetscEnum
#define PCPARMSLocalType PetscEnum
#define PCFieldSplitSchurFactType PetscEnum
#define CoarseProblemType PetscEnum
#define PCGAMGType character*(80)
#define PCGAMGClassicalType character*(80)
!
! GAMG types
!
#define PCGAMGAGG 'agg'
#define PCGAMGGEO  'geo'
#define PCGAMGCLASSICAL 'classical'
!
! GAMG classical types
!
#define PCGAMGCLASSICALDIRECT   'direct'
#define PCGAMGCLASSICALSTANDARD 'standard'

!
!  Various preconditioners
!
#define PCNONE 'none'
#define PCJACOBI 'jacobi'
#define PCSOR 'sor'
#define PCLU 'lu'
#define PCSHELL 'shell'
#define PCBJACOBI 'bjacobi'
#define PCMG 'mg'
#define PCEISENSTAT 'eisenstat'
#define PCILU 'ilu'
#define PCICC 'icc'
#define PCASM 'asm'
#define PCGASM 'gasm'
#define PCKSP 'ksp'
#define PCCOMPOSITE 'composite'
#define PCREDUNDANT 'redundant'
#define PCSPAI 'spai'
#define PCNN 'nn'
#define PCCHOLESKY 'cholesky'
#define PCPBJACOBI 'pbjacobi'
#define PCVPBJACOBI 'vpbjacobi'
#define PCMAT 'mat'
#define PCHYPRE 'hypre'
#define PCPARMS 'parms'
#define PCFIELDSPLIT 'fieldsplit'
#define PCTFS 'tfs'
#define PCML 'ml'
#define PCGALERKIN 'galerkin'
#define PCEXOTIC 'exotic'
#define PCSUPPORTGRAPH 'supportgraph'
#define PCCP 'cp'
#define PCBFBT 'bfbt'
#define PCLSC 'lsc'
#define PCPYTHON 'python'
#define PCPFMG 'pfmg'
#define PCSYSPFMG 'syspfmg'
#define PCREDISTRIBUTE 'redistribute'
#define PCSVD 'svd'
#define PCGAMG 'gamg'
#define PCBDDC 'bddc'
#define PCPATCH 'patch'
#define PCDEFLATION 'deflation'

#define PCMGType PetscEnum
#define PCMGCycleType PetscEnum
#define PCMGGalerkinType PetscEnum
#define PCExoticType PetscEnum
#define PCDeflationSpaceType PetscEnum
#define PCFailedReason PetscEnum
#endif
