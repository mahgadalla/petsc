#include <../src/vec/is/sf/impls/basic/allgatherv/sfallgatherv.h>
#include <../src/vec/is/sf/impls/basic/allgather/sfallgather.h>
#include <../src/vec/is/sf/impls/basic/gatherv/sfgatherv.h>

typedef PetscSFPack_Allgatherv PetscSFPack_Alltoall;
#define PetscSFPackGet_Alltoall PetscSFPackGet_Allgatherv

/* Reuse the type. The difference is some fields (i.e., displs, recvcounts) are not used, which is not a big deal */
typedef PetscSF_Allgatherv PetscSF_Alltoall;

/*===================================================================================*/
/*              Implementations of SF public APIs                                    */
/*===================================================================================*/
static PetscErrorCode PetscSFGetGraph_Alltoall(PetscSF sf,PetscInt *nroots,PetscInt *nleaves,const PetscInt **ilocal,const PetscSFNode **iremote)
{
  PetscErrorCode ierr;
  PetscInt       i;

  PetscFunctionBegin;
  if (nroots)  *nroots  = sf->nroots;
  if (nleaves) *nleaves = sf->nleaves;
  if (ilocal)  *ilocal  = NULL; /* Contiguous local indices */
  if (iremote) {
    if (!sf->remote) {
      ierr = PetscMalloc1(sf->nleaves,&sf->remote);CHKERRQ(ierr);
      sf->remote_alloc = sf->remote;
      for (i=0; i<sf->nleaves; i++) {
        sf->remote[i].rank  = i;
        sf->remote[i].index = i;
      }
    }
    *iremote = sf->remote;
  }
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFBcastAndOpBegin_Alltoall(PetscSF sf,MPI_Datatype unit,const void *rootdata,void *leafdata,MPI_Op op)
{
  PetscErrorCode       ierr;
  PetscSFPack_Alltoall link;
  MPI_Comm             comm;
  void                 *recvbuf;

  PetscFunctionBegin;
  ierr = PetscSFPackGet_Alltoall(sf,unit,rootdata,leafdata,&link);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject)sf,&comm);CHKERRQ(ierr);

  if (op != MPIU_REPLACE) {
    if (!link->leaf) {ierr = PetscMalloc(sf->nleaves*link->unitbytes,&link->leaf);CHKERRQ(ierr);}
    recvbuf = link->leaf;
  } else {
    recvbuf = leafdata;
  }
  ierr = MPIU_Ialltoall(rootdata,1,unit,recvbuf,1,unit,comm,&link->request);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFReduceBegin_Alltoall(PetscSF sf,MPI_Datatype unit,const void *leafdata,void *rootdata,MPI_Op op)
{
  PetscErrorCode       ierr;
  PetscSFPack_Alltoall link;
  MPI_Comm             comm;
  void                 *recvbuf;

  PetscFunctionBegin;
  ierr = PetscSFPackGet_Alltoall(sf,unit,rootdata,leafdata,&link);CHKERRQ(ierr);
  ierr = PetscObjectGetComm((PetscObject)sf,&comm);CHKERRQ(ierr);

  if (op != MPIU_REPLACE) {
    if (!link->root) {ierr = PetscMalloc(sf->nroots*link->unitbytes,&link->root);CHKERRQ(ierr);}
    recvbuf = link->root;
  } else {
    recvbuf = rootdata;
  }
  ierr = MPIU_Ialltoall(leafdata,1,unit,recvbuf,1,unit,comm,&link->request);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFCreateLocalSF_Alltoall(PetscSF sf,PetscSF *out)
{
  PetscErrorCode ierr;
  PetscInt       nroots=1,nleaves=1,*ilocal;
  PetscSFNode    *iremote = NULL;
  PetscSF        lsf;
  PetscMPIInt    rank;

  PetscFunctionBegin;
  nroots  = 1;
  nleaves = 1;
  ierr = MPI_Comm_rank(PetscObjectComm((PetscObject)sf),&rank);CHKERRQ(ierr);
  ierr = PetscMalloc1(nleaves,&ilocal);CHKERRQ(ierr);
  ierr = PetscMalloc1(nleaves,&iremote);CHKERRQ(ierr);
  ilocal[0]        = rank;
  iremote[0].rank  = 0;    /* rank in PETSC_COMM_SELF */
  iremote[0].index = rank; /* LocalSF is an embedded SF. Indices are not remapped */

  ierr = PetscSFCreate(PETSC_COMM_SELF,&lsf);CHKERRQ(ierr);
  ierr = PetscSFSetGraph(lsf,nroots,nleaves,NULL/*contiguous leaves*/,PETSC_OWN_POINTER,iremote,PETSC_OWN_POINTER);CHKERRQ(ierr);
  ierr = PetscSFSetUp(lsf);CHKERRQ(ierr);
  *out = lsf;
  PetscFunctionReturn(0);
}

static PetscErrorCode PetscSFCreateEmbeddedSF_Alltoall(PetscSF sf,PetscInt nselected,const PetscInt *selected,PetscSF *newsf)
{
  PetscErrorCode ierr;
  PetscInt       i,*tmproots,*ilocal,ndranks,ndiranks;
  PetscSFNode    *iremote;
  PetscMPIInt    nroots,*roots,nleaves,*leaves,rank;
  MPI_Comm       comm;
  PetscSF_Basic  *bas;
  PetscSF        esf;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)sf,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);

  /* Uniq selected[] and store the result in roots[] */
  ierr = PetscMalloc1(nselected,&tmproots);CHKERRQ(ierr);
  ierr = PetscArraycpy(tmproots,selected,nselected);CHKERRQ(ierr);
  ierr = PetscSortRemoveDupsInt(&nselected,tmproots);CHKERRQ(ierr); /* nselected might be changed */
  if (tmproots[0] < 0 || tmproots[nselected-1] >= sf->nroots) SETERRQ3(comm,PETSC_ERR_ARG_OUTOFRANGE,"Min/Max root indices %D/%D are not in [0,%D)",tmproots[0],tmproots[nselected-1],sf->nroots);
  nroots = nselected;   /* For Alltoall, we know root indices will not overflow MPI_INT */
  ierr   = PetscMalloc1(nselected,&roots);CHKERRQ(ierr);
  for (i=0; i<nselected; i++) roots[i] = tmproots[i];
  ierr   = PetscFree(tmproots);CHKERRQ(ierr);

  /* Find out which leaves are still connected to roots in the embedded sf. Expect PetscCommBuildTwoSided is more scalable than MPI_Alltoall */
  ierr   = PetscCommBuildTwoSided(comm,0/*empty msg*/,MPI_INT/*fake*/,nroots,roots,NULL/*todata*/,&nleaves,&leaves,NULL/*fromdata*/);CHKERRQ(ierr);

  /* Move myself ahead if rank is in leaves[], since I am a distinguished rank */
  ndranks = 0;
  for (i=0; i<nleaves; i++) {
    if (leaves[i] == rank) {leaves[i] = -rank; ndranks = 1; break;}
  }
  ierr = PetscSortMPIInt(nleaves,leaves);CHKERRQ(ierr);
  if (nleaves && leaves[0] < 0) leaves[0] = rank;

  /* Build esf and fill its fields manually (without calling PetscSFSetUp) */
  ierr = PetscMalloc1(nleaves,&ilocal);CHKERRQ(ierr);
  ierr = PetscMalloc1(nleaves,&iremote);CHKERRQ(ierr);
  for (i=0; i<nleaves; i++) { /* 1:1 map from roots to leaves */
    ilocal[i]        = leaves[i];
    iremote[i].rank  = leaves[i];
    iremote[i].index = leaves[i];
  }
  ierr = PetscSFCreate(comm,&esf);CHKERRQ(ierr);
  ierr = PetscSFSetType(esf,PETSCSFBASIC);CHKERRQ(ierr); /* This optimized routine can only create a basic sf */
  ierr = PetscSFSetGraph(esf,sf->nleaves,nleaves,ilocal,PETSC_OWN_POINTER,iremote,PETSC_OWN_POINTER);CHKERRQ(ierr);

  /* As if we are calling PetscSFSetUpRanks(esf,self's group) */
  ierr = PetscMalloc4(nleaves,&esf->ranks,nleaves+1,&esf->roffset,nleaves,&esf->rmine,nleaves,&esf->rremote);CHKERRQ(ierr);
  esf->nranks     = nleaves;
  esf->ndranks    = ndranks;
  esf->roffset[0] = 0;
  for (i=0; i<nleaves; i++) {
    esf->ranks[i]     = leaves[i];
    esf->roffset[i+1] = i+1;
    esf->rmine[i]     = leaves[i];
    esf->rremote[i]   = leaves[i];
  }

  /* Set up esf->data, the incoming communication (i.e., recv info), which is usually done by PetscSFSetUp_Basic */
  bas  = (PetscSF_Basic*)esf->data;
  ierr = PetscMalloc2(nroots,&bas->iranks,nroots+1,&bas->ioffset);CHKERRQ(ierr);
  ierr = PetscMalloc1(nroots,&bas->irootloc);CHKERRQ(ierr);
  /* Move myself ahead if rank is in roots[], since I am a distinguished irank */
  ndiranks = 0;
  for (i=0; i<nroots; i++) {
    if (roots[i] == rank) {roots[i] = -rank; ndiranks = 1; break;}
  }
  ierr = PetscSortMPIInt(nroots,roots);CHKERRQ(ierr);
  if (nroots && roots[0] < 0) roots[0] = rank;

  bas->niranks    = nroots;
  bas->ndiranks   = ndiranks;
  bas->ioffset[0] = 0;
  bas->itotal     = nroots;
  for (i=0; i<nroots; i++) {
    bas->iranks[i]    = roots[i];
    bas->ioffset[i+1] = i+1;
    bas->irootloc[i]  = roots[i];
  }

  *newsf = esf;
  PetscFunctionReturn(0);
}

PETSC_INTERN PetscErrorCode PetscSFCreate_Alltoall(PetscSF sf)
{
  PetscErrorCode   ierr;
  PetscSF_Alltoall *dat = (PetscSF_Alltoall*)sf->data;

  PetscFunctionBegin;
  /* Inherit from Allgatherv. It is astonishing Alltoall can inherit so much from Allgather(v) */
  sf->ops->Destroy         = PetscSFDestroy_Allgatherv;
  sf->ops->Reset           = PetscSFReset_Allgatherv;
  sf->ops->BcastAndOpEnd   = PetscSFBcastAndOpEnd_Allgatherv;
  sf->ops->ReduceEnd       = PetscSFReduceEnd_Allgatherv;
  sf->ops->FetchAndOpEnd   = PetscSFFetchAndOpEnd_Allgatherv;
  sf->ops->GetRootRanks    = PetscSFGetRootRanks_Allgatherv;

  /* Inherit from Allgather. Every process gathers equal-sized data from others, which enables this inheritance. */
  sf->ops->GetLeafRanks    = PetscSFGetLeafRanks_Allgatherv;

  /* Inherit from Gatherv. Each root has only one leaf connected, which enables this inheritance */
  sf->ops->FetchAndOpBegin  = PetscSFFetchAndOpBegin_Gatherv;

  /* Alltoall stuff */
  sf->ops->GetGraph         = PetscSFGetGraph_Alltoall;
  sf->ops->BcastAndOpBegin  = PetscSFBcastAndOpBegin_Alltoall;
  sf->ops->ReduceBegin      = PetscSFReduceBegin_Alltoall;
  sf->ops->CreateLocalSF    = PetscSFCreateLocalSF_Alltoall;
  sf->ops->CreateEmbeddedSF = PetscSFCreateEmbeddedSF_Alltoall;

  ierr = PetscNewLog(sf,&dat);CHKERRQ(ierr);
  sf->data = (void*)dat;
  PetscFunctionReturn(0);
}


