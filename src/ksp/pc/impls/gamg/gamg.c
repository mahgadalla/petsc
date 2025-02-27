/*
 GAMG geometric-algebric multigrid PC - Mark Adams 2011
 */
#include <petsc/private/matimpl.h>
#include <../src/ksp/pc/impls/gamg/gamg.h>           /*I "petscpc.h" I*/
#include <../src/ksp/pc/impls/bjacobi/bjacobi.h> /* Hack to access same_local_solves */

#if defined PETSC_GAMG_USE_LOG
PetscLogEvent petsc_gamg_setup_events[NUM_SET];
#endif

#if defined PETSC_USE_LOG
PetscLogEvent PC_GAMGGraph_AGG;
PetscLogEvent PC_GAMGGraph_GEO;
PetscLogEvent PC_GAMGCoarsen_AGG;
PetscLogEvent PC_GAMGCoarsen_GEO;
PetscLogEvent PC_GAMGProlongator_AGG;
PetscLogEvent PC_GAMGProlongator_GEO;
PetscLogEvent PC_GAMGOptProlongator_AGG;
#endif

/* #define GAMG_STAGES */
#if (defined PETSC_GAMG_USE_LOG && defined GAMG_STAGES)
static PetscLogStage gamg_stages[PETSC_GAMG_MAXLEVELS];
#endif

static PetscFunctionList GAMGList = 0;
static PetscBool PCGAMGPackageInitialized;

/* ----------------------------------------------------------------------------- */
PetscErrorCode PCReset_GAMG(PC pc)
{
  PetscErrorCode ierr;
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  ierr = PetscFree(pc_gamg->data);CHKERRQ(ierr);
  pc_gamg->data_sz = 0;
  ierr = PetscFree(pc_gamg->orig_data);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   PCGAMGCreateLevel_GAMG: create coarse op with RAP.  repartition and/or reduce number
     of active processors.

   Input Parameter:
   . pc - parameters + side effect: coarse data in 'pc_gamg->data' and
          'pc_gamg->data_sz' are changed via repartitioning/reduction.
   . Amat_fine - matrix on this fine (k) level
   . cr_bs - coarse block size
   In/Output Parameter:
   . a_P_inout - prolongation operator to the next level (k-->k-1)
   . a_nactive_proc - number of active procs
   Output Parameter:
   . a_Amat_crs - coarse matrix that is created (k-1)
*/

static PetscErrorCode PCGAMGCreateLevel_GAMG(PC pc,Mat Amat_fine,PetscInt cr_bs,Mat *a_P_inout,Mat *a_Amat_crs,PetscMPIInt *a_nactive_proc,IS * Pcolumnperm, PetscBool is_last)
{
  PetscErrorCode  ierr;
  PC_MG           *mg         = (PC_MG*)pc->data;
  PC_GAMG         *pc_gamg    = (PC_GAMG*)mg->innerctx;
  Mat             Cmat,Pold=*a_P_inout;
  MPI_Comm        comm;
  PetscMPIInt     rank,size,new_size,nactive=*a_nactive_proc;
  PetscInt        ncrs_eq,ncrs,f_bs;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)Amat_fine,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm, &rank);CHKERRQ(ierr);
  ierr = MPI_Comm_size(comm, &size);CHKERRQ(ierr);
  ierr = MatGetBlockSize(Amat_fine, &f_bs);CHKERRQ(ierr);
  ierr = MatPtAP(Amat_fine, Pold, MAT_INITIAL_MATRIX, 2.0, &Cmat);CHKERRQ(ierr);

  /* set 'ncrs' (nodes), 'ncrs_eq' (equations)*/
  ierr = MatGetLocalSize(Cmat, &ncrs_eq, NULL);CHKERRQ(ierr);
  if (pc_gamg->data_cell_rows>0) {
    ncrs = pc_gamg->data_sz/pc_gamg->data_cell_cols/pc_gamg->data_cell_rows;
  } else {
    PetscInt  bs;
    ierr = MatGetBlockSize(Cmat, &bs);CHKERRQ(ierr);
    ncrs = ncrs_eq/bs;
  }

  /* get number of PEs to make active 'new_size', reduce, can be any integer 1-P */
  if (is_last && !pc_gamg->use_parallel_coarse_grid_solver) new_size = 1;
  else {
    PetscInt ncrs_eq_glob;
    ierr     = MatGetSize(Cmat, &ncrs_eq_glob, NULL);CHKERRQ(ierr);
    new_size = (PetscMPIInt)((float)ncrs_eq_glob/(float)pc_gamg->min_eq_proc + 0.5); /* hardwire min. number of eq/proc */
    if (!new_size) new_size = 1; /* not likely, posible? */
    else if (new_size >= nactive) new_size = nactive; /* no change, rare */
  }

  if (Pcolumnperm) *Pcolumnperm = NULL;

  if (new_size==nactive) {
    *a_Amat_crs = Cmat; /* output - no repartitioning or reduction - could bail here */
    /* we know that the grid structure can be reused in MatPtAP */
  } else { /* reduce active processors - we know that the grid structure can NOT be reused in MatPtAP */
    PetscInt       *counts,*newproc_idx,ii,jj,kk,strideNew,*tidx,ncrs_new,ncrs_eq_new,nloc_old;
    IS             is_eq_newproc,is_eq_num,is_eq_num_prim,new_eq_indices;
    nloc_old = ncrs_eq/cr_bs;
    if (ncrs_eq % cr_bs) SETERRQ2(PETSC_COMM_SELF,PETSC_ERR_PLIB,"ncrs_eq %D not divisible by cr_bs %D",ncrs_eq,cr_bs);
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET12],0,0,0,0);CHKERRQ(ierr);
#endif
    /* make 'is_eq_newproc' */
    ierr = PetscMalloc1(size, &counts);CHKERRQ(ierr);
    if (pc_gamg->repart) {
      /* Repartition Cmat_{k} and move colums of P^{k}_{k-1} and coordinates of primal part accordingly */
      Mat adj;

      ierr = PetscInfo3(pc,"Repartition: size (active): %D --> %D, %D local equations\n",*a_nactive_proc,new_size,ncrs_eq);CHKERRQ(ierr);

      /* get 'adj' */
      if (cr_bs == 1) {
        ierr = MatConvert(Cmat, MATMPIADJ, MAT_INITIAL_MATRIX, &adj);CHKERRQ(ierr);
      } else {
        /* make a scalar matrix to partition (no Stokes here) */
        Mat               tMat;
        PetscInt          Istart_crs,Iend_crs,ncols,jj,Ii;
        const PetscScalar *vals;
        const PetscInt    *idx;
        PetscInt          *d_nnz, *o_nnz, M, N;
        static PetscInt   llev = 0;
        MatType           mtype;

        ierr = PetscMalloc2(ncrs, &d_nnz,ncrs, &o_nnz);CHKERRQ(ierr);
        ierr = MatGetOwnershipRange(Cmat, &Istart_crs, &Iend_crs);CHKERRQ(ierr);
        ierr = MatGetSize(Cmat, &M, &N);CHKERRQ(ierr);
        for (Ii = Istart_crs, jj = 0; Ii < Iend_crs; Ii += cr_bs, jj++) {
          ierr      = MatGetRow(Cmat,Ii,&ncols,0,0);CHKERRQ(ierr);
          d_nnz[jj] = ncols/cr_bs;
          o_nnz[jj] = ncols/cr_bs;
          ierr      = MatRestoreRow(Cmat,Ii,&ncols,0,0);CHKERRQ(ierr);
          if (d_nnz[jj] > ncrs) d_nnz[jj] = ncrs;
          if (o_nnz[jj] > (M/cr_bs-ncrs)) o_nnz[jj] = M/cr_bs-ncrs;
        }

        ierr = MatGetType(Amat_fine,&mtype);CHKERRQ(ierr);
        ierr = MatCreate(comm, &tMat);CHKERRQ(ierr);
        ierr = MatSetSizes(tMat, ncrs, ncrs,PETSC_DETERMINE, PETSC_DETERMINE);CHKERRQ(ierr);
        ierr = MatSetType(tMat,mtype);CHKERRQ(ierr);
        ierr = MatSeqAIJSetPreallocation(tMat,0,d_nnz);CHKERRQ(ierr);
        ierr = MatMPIAIJSetPreallocation(tMat,0,d_nnz,0,o_nnz);CHKERRQ(ierr);
        ierr = PetscFree2(d_nnz,o_nnz);CHKERRQ(ierr);

        for (ii = Istart_crs; ii < Iend_crs; ii++) {
          PetscInt dest_row = ii/cr_bs;
          ierr = MatGetRow(Cmat,ii,&ncols,&idx,&vals);CHKERRQ(ierr);
          for (jj = 0; jj < ncols; jj++) {
            PetscInt    dest_col = idx[jj]/cr_bs;
            PetscScalar v        = 1.0;
            ierr = MatSetValues(tMat,1,&dest_row,1,&dest_col,&v,ADD_VALUES);CHKERRQ(ierr);
          }
          ierr = MatRestoreRow(Cmat,ii,&ncols,&idx,&vals);CHKERRQ(ierr);
        }
        ierr = MatAssemblyBegin(tMat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);
        ierr = MatAssemblyEnd(tMat,MAT_FINAL_ASSEMBLY);CHKERRQ(ierr);

        if (llev++ == -1) {
          PetscViewer viewer; char fname[32];
          ierr = PetscSNPrintf(fname,sizeof(fname),"part_mat_%D.mat",llev);CHKERRQ(ierr);
          PetscViewerBinaryOpen(comm,fname,FILE_MODE_WRITE,&viewer);
          ierr = MatView(tMat, viewer);CHKERRQ(ierr);
          ierr = PetscViewerDestroy(&viewer);CHKERRQ(ierr);
        }
        ierr = MatConvert(tMat, MATMPIADJ, MAT_INITIAL_MATRIX, &adj);CHKERRQ(ierr);
        ierr = MatDestroy(&tMat);CHKERRQ(ierr);
      } /* create 'adj' */

      { /* partition: get newproc_idx */
        char            prefix[256];
        const char      *pcpre;
        const PetscInt  *is_idx;
        MatPartitioning mpart;
        IS              proc_is;
        PetscInt        targetPE;

        ierr = MatPartitioningCreate(comm, &mpart);CHKERRQ(ierr);
        ierr = MatPartitioningSetAdjacency(mpart, adj);CHKERRQ(ierr);
        ierr = PCGetOptionsPrefix(pc, &pcpre);CHKERRQ(ierr);
        ierr = PetscSNPrintf(prefix,sizeof(prefix),"%spc_gamg_",pcpre ? pcpre : "");CHKERRQ(ierr);
        ierr = PetscObjectSetOptionsPrefix((PetscObject)mpart,prefix);CHKERRQ(ierr);
        ierr = MatPartitioningSetFromOptions(mpart);CHKERRQ(ierr);
        ierr = MatPartitioningSetNParts(mpart, new_size);CHKERRQ(ierr);
        ierr = MatPartitioningApply(mpart, &proc_is);CHKERRQ(ierr);
        ierr = MatPartitioningDestroy(&mpart);CHKERRQ(ierr);

        /* collect IS info */
        ierr     = PetscMalloc1(ncrs_eq, &newproc_idx);CHKERRQ(ierr);
        ierr     = ISGetIndices(proc_is, &is_idx);CHKERRQ(ierr);
        targetPE = 1; /* bring to "front" of machine */
        /*targetPE = size/new_size;*/ /* spread partitioning across machine */
        for (kk = jj = 0 ; kk < nloc_old ; kk++) {
          for (ii = 0 ; ii < cr_bs ; ii++, jj++) {
            newproc_idx[jj] = is_idx[kk] * targetPE; /* distribution */
          }
        }
        ierr = ISRestoreIndices(proc_is, &is_idx);CHKERRQ(ierr);
        ierr = ISDestroy(&proc_is);CHKERRQ(ierr);
      }
      ierr = MatDestroy(&adj);CHKERRQ(ierr);

      ierr = ISCreateGeneral(comm, ncrs_eq, newproc_idx, PETSC_COPY_VALUES, &is_eq_newproc);CHKERRQ(ierr);
      ierr = PetscFree(newproc_idx);CHKERRQ(ierr);
    } else { /* simple aggreagtion of parts -- 'is_eq_newproc' */
      PetscInt rfactor,targetPE;

      /* find factor */
      if (new_size == 1) rfactor = size; /* easy */
      else {
        PetscReal best_fact = 0.;
        jj = -1;
        for (kk = 1 ; kk <= size ; kk++) {
          if (!(size%kk)) { /* a candidate */
            PetscReal nactpe = (PetscReal)size/(PetscReal)kk, fact = nactpe/(PetscReal)new_size;
            if (fact > 1.0) fact = 1./fact; /* keep fact < 1 */
            if (fact > best_fact) {
              best_fact = fact; jj = kk;
            }
          }
        }
        if (jj != -1) rfactor = jj;
        else rfactor = 1; /* does this happen .. a prime */
      }
      new_size = size/rfactor;

      if (new_size==nactive) {
        *a_Amat_crs = Cmat; /* output - no repartitioning or reduction, bail out because nested here */
        ierr        = PetscFree(counts);CHKERRQ(ierr);
        ierr = PetscInfo2(pc,"Aggregate processors noop: new_size=%D, neq(loc)=%D\n",new_size,ncrs_eq);CHKERRQ(ierr);
#if defined PETSC_GAMG_USE_LOG
        ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET12],0,0,0,0);CHKERRQ(ierr);
#endif
        PetscFunctionReturn(0);
      }

      ierr = PetscInfo1(pc,"Number of equations (loc) %D with simple aggregation\n",ncrs_eq);CHKERRQ(ierr);
      targetPE = rank/rfactor;
      ierr     = ISCreateStride(comm, ncrs_eq, targetPE, 0, &is_eq_newproc);CHKERRQ(ierr);
    } /* end simple 'is_eq_newproc' */

    /*
      Create an index set from the is_eq_newproc index set to indicate the mapping TO
    */
    ierr = ISPartitioningToNumbering(is_eq_newproc, &is_eq_num);CHKERRQ(ierr);
    is_eq_num_prim = is_eq_num;
    /*
      Determine how many equations/vertices are assigned to each processor
    */
    ierr        = ISPartitioningCount(is_eq_newproc, size, counts);CHKERRQ(ierr);
    ncrs_eq_new = counts[rank];
    ierr        = ISDestroy(&is_eq_newproc);CHKERRQ(ierr);
    ncrs_new = ncrs_eq_new/cr_bs; /* eqs */

    ierr = PetscFree(counts);CHKERRQ(ierr);
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET12],0,0,0,0);CHKERRQ(ierr);
#endif
    /* data movement scope -- this could be moved to subclasses so that we don't try to cram all auxilary data into some complex abstracted thing */
    {
      Vec            src_crd, dest_crd;
      const PetscInt *idx,ndata_rows=pc_gamg->data_cell_rows,ndata_cols=pc_gamg->data_cell_cols,node_data_sz=ndata_rows*ndata_cols;
      VecScatter     vecscat;
      PetscScalar    *array;
      IS isscat;
      /* move data (for primal equations only) */
      /* Create a vector to contain the newly ordered element information */
      ierr = VecCreate(comm, &dest_crd);CHKERRQ(ierr);
      ierr = VecSetSizes(dest_crd, node_data_sz*ncrs_new, PETSC_DECIDE);CHKERRQ(ierr);
      ierr = VecSetType(dest_crd,VECSTANDARD);CHKERRQ(ierr); /* this is needed! */
      /*
	There are 'ndata_rows*ndata_cols' data items per node, (one can think of the vectors of having
	a block size of ...).  Note, ISs are expanded into equation space by 'cr_bs'.
      */
      ierr = PetscMalloc1(ncrs*node_data_sz, &tidx);CHKERRQ(ierr);
      ierr = ISGetIndices(is_eq_num_prim, &idx);CHKERRQ(ierr);
      for (ii=0,jj=0; ii<ncrs; ii++) {
	PetscInt id = idx[ii*cr_bs]/cr_bs; /* get node back */
	for (kk=0; kk<node_data_sz; kk++, jj++) tidx[jj] = id*node_data_sz + kk;
      }
      ierr = ISRestoreIndices(is_eq_num_prim, &idx);CHKERRQ(ierr);
      ierr = ISCreateGeneral(comm, node_data_sz*ncrs, tidx, PETSC_COPY_VALUES, &isscat);CHKERRQ(ierr);
      ierr = PetscFree(tidx);CHKERRQ(ierr);
      /*
	Create a vector to contain the original vertex information for each element
      */
      ierr = VecCreateSeq(PETSC_COMM_SELF, node_data_sz*ncrs, &src_crd);CHKERRQ(ierr);
      for (jj=0; jj<ndata_cols; jj++) {
	const PetscInt stride0=ncrs*pc_gamg->data_cell_rows;
	for (ii=0; ii<ncrs; ii++) {
	  for (kk=0; kk<ndata_rows; kk++) {
	    PetscInt    ix = ii*ndata_rows + kk + jj*stride0, jx = ii*node_data_sz + kk*ndata_cols + jj;
	    PetscScalar tt = (PetscScalar)pc_gamg->data[ix];
	    ierr = VecSetValues(src_crd, 1, &jx, &tt, INSERT_VALUES);CHKERRQ(ierr);
	  }
	}
      }
      ierr = VecAssemblyBegin(src_crd);CHKERRQ(ierr);
      ierr = VecAssemblyEnd(src_crd);CHKERRQ(ierr);
      /*
	Scatter the element vertex information (still in the original vertex ordering)
	to the correct processor
      */
      ierr = VecScatterCreate(src_crd, NULL, dest_crd, isscat, &vecscat);CHKERRQ(ierr);
      ierr = ISDestroy(&isscat);CHKERRQ(ierr);
      ierr = VecScatterBegin(vecscat,src_crd,dest_crd,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterEnd(vecscat,src_crd,dest_crd,INSERT_VALUES,SCATTER_FORWARD);CHKERRQ(ierr);
      ierr = VecScatterDestroy(&vecscat);CHKERRQ(ierr);
      ierr = VecDestroy(&src_crd);CHKERRQ(ierr);
      /*
	Put the element vertex data into a new allocation of the gdata->ele
      */
      ierr = PetscFree(pc_gamg->data);CHKERRQ(ierr);
      ierr = PetscMalloc1(node_data_sz*ncrs_new, &pc_gamg->data);CHKERRQ(ierr);
      
      pc_gamg->data_sz = node_data_sz*ncrs_new;
      strideNew        = ncrs_new*ndata_rows;
      
      ierr = VecGetArray(dest_crd, &array);CHKERRQ(ierr);
      for (jj=0; jj<ndata_cols; jj++) {
	for (ii=0; ii<ncrs_new; ii++) {
	  for (kk=0; kk<ndata_rows; kk++) {
	    PetscInt ix = ii*ndata_rows + kk + jj*strideNew, jx = ii*node_data_sz + kk*ndata_cols + jj;
	    pc_gamg->data[ix] = PetscRealPart(array[jx]);
	  }
	}
      }
      ierr = VecRestoreArray(dest_crd, &array);CHKERRQ(ierr);
      ierr = VecDestroy(&dest_crd);CHKERRQ(ierr);
    }
    /* move A and P (columns) with new layout */
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET13],0,0,0,0);CHKERRQ(ierr);
#endif
    /*
      Invert for MatCreateSubMatrix
    */
    ierr = ISInvertPermutation(is_eq_num, ncrs_eq_new, &new_eq_indices);CHKERRQ(ierr);
    ierr = ISSort(new_eq_indices);CHKERRQ(ierr); /* is this needed? */
    ierr = ISSetBlockSize(new_eq_indices, cr_bs);CHKERRQ(ierr);
    if (is_eq_num != is_eq_num_prim) {
      ierr = ISDestroy(&is_eq_num_prim);CHKERRQ(ierr); /* could be same as 'is_eq_num' */
    }
    if (Pcolumnperm) {
      ierr = PetscObjectReference((PetscObject)new_eq_indices);CHKERRQ(ierr);
      *Pcolumnperm = new_eq_indices;
    }
    ierr = ISDestroy(&is_eq_num);CHKERRQ(ierr);
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET13],0,0,0,0);CHKERRQ(ierr);
    ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET14],0,0,0,0);CHKERRQ(ierr);
#endif
    /* 'a_Amat_crs' output */
    {
      Mat mat;
      ierr        = MatCreateSubMatrix(Cmat, new_eq_indices, new_eq_indices, MAT_INITIAL_MATRIX, &mat);CHKERRQ(ierr);
      *a_Amat_crs = mat;
    }
    ierr = MatDestroy(&Cmat);CHKERRQ(ierr);

#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET14],0,0,0,0);CHKERRQ(ierr);
#endif
    /* prolongator */
    {
      IS       findices;
      PetscInt Istart,Iend;
      Mat      Pnew;

      ierr = MatGetOwnershipRange(Pold, &Istart, &Iend);CHKERRQ(ierr);
#if defined PETSC_GAMG_USE_LOG
      ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET15],0,0,0,0);CHKERRQ(ierr);
#endif
      ierr = ISCreateStride(comm,Iend-Istart,Istart,1,&findices);CHKERRQ(ierr);
      ierr = ISSetBlockSize(findices,f_bs);CHKERRQ(ierr);
      ierr = MatCreateSubMatrix(Pold, findices, new_eq_indices, MAT_INITIAL_MATRIX, &Pnew);CHKERRQ(ierr);
      ierr = ISDestroy(&findices);CHKERRQ(ierr);

#if defined PETSC_GAMG_USE_LOG
      ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET15],0,0,0,0);CHKERRQ(ierr);
#endif
      ierr = MatDestroy(a_P_inout);CHKERRQ(ierr);

      /* output - repartitioned */
      *a_P_inout = Pnew;
    }
    ierr = ISDestroy(&new_eq_indices);CHKERRQ(ierr);

    *a_nactive_proc = new_size; /* output */
  }
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   PCSetUp_GAMG - Prepares for the use of the GAMG preconditioner
                    by setting data structures and options.

   Input Parameter:
.  pc - the preconditioner context

*/
PetscErrorCode PCSetUp_GAMG(PC pc)
{
  PetscErrorCode ierr;
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;
  Mat            Pmat     = pc->pmat;
  PetscInt       fine_level,level,level1,bs,M,N,qq,lidx,nASMBlocksArr[PETSC_GAMG_MAXLEVELS];
  MPI_Comm       comm;
  PetscMPIInt    rank,size,nactivepe;
  Mat            Aarr[PETSC_GAMG_MAXLEVELS],Parr[PETSC_GAMG_MAXLEVELS];
  IS             *ASMLocalIDsArr[PETSC_GAMG_MAXLEVELS];
  PetscLogDouble nnz0=0.,nnztot=0.;
  MatInfo        info;
  PetscBool      is_last = PETSC_FALSE;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)pc,&comm);CHKERRQ(ierr);
  ierr = MPI_Comm_rank(comm,&rank);CHKERRQ(ierr);
  ierr = MPI_Comm_size(comm,&size);CHKERRQ(ierr);

  if (pc_gamg->setup_count++ > 0) {
    if ((PetscBool)(!pc_gamg->reuse_prol)) {
      /* reset everything */
      ierr = PCReset_MG(pc);CHKERRQ(ierr);
      pc->setupcalled = 0;
    } else {
      PC_MG_Levels **mglevels = mg->levels;
      /* just do Galerkin grids */
      Mat          B,dA,dB;

      if (!pc->setupcalled) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_PLIB,"PCSetUp() has not been called yet");
      if (pc_gamg->Nlevels > 1) {
        /* currently only handle case where mat and pmat are the same on coarser levels */
        ierr = KSPGetOperators(mglevels[pc_gamg->Nlevels-1]->smoothd,&dA,&dB);CHKERRQ(ierr);
        /* (re)set to get dirty flag */
        ierr = KSPSetOperators(mglevels[pc_gamg->Nlevels-1]->smoothd,dA,dB);CHKERRQ(ierr);

        for (level=pc_gamg->Nlevels-2; level>=0; level--) {
          /* 2nd solve, matrix structure can change from repartitioning or process reduction but don't know if we have process reduction here. Should fix */
          if (pc_gamg->setup_count==2 /* && pc_gamg->repart||reduction */) {
            ierr = PetscInfo2(pc,"new RAP after first solve level %D, %D setup\n",level,pc_gamg->setup_count);CHKERRQ(ierr);
            ierr = MatPtAP(dB,mglevels[level+1]->interpolate,MAT_INITIAL_MATRIX,2.0,&B);CHKERRQ(ierr);
            ierr = MatDestroy(&mglevels[level]->A);CHKERRQ(ierr);
            mglevels[level]->A = B;
          } else {
            ierr = PetscInfo2(pc,"RAP after first solve reusing matrix level %D, %D setup\n",level,pc_gamg->setup_count);CHKERRQ(ierr);
            ierr = KSPGetOperators(mglevels[level]->smoothd,NULL,&B);CHKERRQ(ierr);
            ierr = MatPtAP(dB,mglevels[level+1]->interpolate,MAT_REUSE_MATRIX,1.0,&B);CHKERRQ(ierr);
          }
          ierr = KSPSetOperators(mglevels[level]->smoothd,B,B);CHKERRQ(ierr);
          dB   = B;
        }
      }

      ierr = PCSetUp_MG(pc);CHKERRQ(ierr);
      PetscFunctionReturn(0);
    }
  }

  if (!pc_gamg->data) {
    if (pc_gamg->orig_data) {
      ierr = MatGetBlockSize(Pmat, &bs);CHKERRQ(ierr);
      ierr = MatGetLocalSize(Pmat, &qq, NULL);CHKERRQ(ierr);

      pc_gamg->data_sz        = (qq/bs)*pc_gamg->orig_data_cell_rows*pc_gamg->orig_data_cell_cols;
      pc_gamg->data_cell_rows = pc_gamg->orig_data_cell_rows;
      pc_gamg->data_cell_cols = pc_gamg->orig_data_cell_cols;

      ierr = PetscMalloc1(pc_gamg->data_sz, &pc_gamg->data);CHKERRQ(ierr);
      for (qq=0; qq<pc_gamg->data_sz; qq++) pc_gamg->data[qq] = pc_gamg->orig_data[qq];
    } else {
      if (!pc_gamg->ops->createdefaultdata) SETERRQ(comm,PETSC_ERR_PLIB,"'createdefaultdata' not set(?) need to support NULL data");
      ierr = pc_gamg->ops->createdefaultdata(pc,Pmat);CHKERRQ(ierr);
    }
  }

  /* cache original data for reuse */
  if (!pc_gamg->orig_data && (PetscBool)(!pc_gamg->reuse_prol)) {
    ierr = PetscMalloc1(pc_gamg->data_sz, &pc_gamg->orig_data);CHKERRQ(ierr);
    for (qq=0; qq<pc_gamg->data_sz; qq++) pc_gamg->orig_data[qq] = pc_gamg->data[qq];
    pc_gamg->orig_data_cell_rows = pc_gamg->data_cell_rows;
    pc_gamg->orig_data_cell_cols = pc_gamg->data_cell_cols;
  }

  /* get basic dims */
  ierr = MatGetBlockSize(Pmat, &bs);CHKERRQ(ierr);
  ierr = MatGetSize(Pmat, &M, &N);CHKERRQ(ierr);

  ierr = MatGetInfo(Pmat,MAT_GLOBAL_SUM,&info);CHKERRQ(ierr); /* global reduction */
  nnz0   = info.nz_used;
  nnztot = info.nz_used;
  ierr = PetscInfo6(pc,"level %d) N=%D, n data rows=%d, n data cols=%d, nnz/row (ave)=%d, np=%d\n",0,M,pc_gamg->data_cell_rows,pc_gamg->data_cell_cols,(int)(nnz0/(PetscReal)M+0.5),size);CHKERRQ(ierr);

  /* Get A_i and R_i */
  for (level=0, Aarr[0]=Pmat, nactivepe = size; level < (pc_gamg->Nlevels-1) && (!level || M>pc_gamg->coarse_eq_limit); level++) {
    pc_gamg->current_level = level;
    level1 = level + 1;
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET1],0,0,0,0);CHKERRQ(ierr);
#if (defined GAMG_STAGES)
    ierr = PetscLogStagePush(gamg_stages[level]);CHKERRQ(ierr);
#endif
#endif
    { /* construct prolongator */
      Mat              Gmat;
      PetscCoarsenData *agg_lists;
      Mat              Prol11;

      ierr = pc_gamg->ops->graph(pc,Aarr[level], &Gmat);CHKERRQ(ierr);
      ierr = pc_gamg->ops->coarsen(pc, &Gmat, &agg_lists);CHKERRQ(ierr);
      ierr = pc_gamg->ops->prolongator(pc,Aarr[level],Gmat,agg_lists,&Prol11);CHKERRQ(ierr);

      /* could have failed to create new level */
      if (Prol11) {
        /* get new block size of coarse matrices */
        ierr = MatGetBlockSizes(Prol11, NULL, &bs);CHKERRQ(ierr);

        if (pc_gamg->ops->optprolongator) {
          /* smooth */
          ierr = pc_gamg->ops->optprolongator(pc, Aarr[level], &Prol11);CHKERRQ(ierr);
        }

        if (pc_gamg->use_aggs_in_asm) {
          PetscInt bs;
          ierr = MatGetBlockSizes(Prol11, &bs, NULL);CHKERRQ(ierr);
          ierr = PetscCDGetASMBlocks(agg_lists, bs, Gmat, &nASMBlocksArr[level], &ASMLocalIDsArr[level]);CHKERRQ(ierr);
        }

        Parr[level1] = Prol11;
      } else Parr[level1] = NULL; /* failed to coarsen */

      ierr = MatDestroy(&Gmat);CHKERRQ(ierr);
      ierr = PetscCDDestroy(agg_lists);CHKERRQ(ierr);
    } /* construct prolongator scope */
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET1],0,0,0,0);CHKERRQ(ierr);
#endif
    if (!level) Aarr[0] = Pmat; /* use Pmat for finest level setup */
    if (!Parr[level1]) { /* failed to coarsen */
      ierr =  PetscInfo1(pc,"Stop gridding, level %D\n",level);CHKERRQ(ierr);
#if defined PETSC_GAMG_USE_LOG && defined GAMG_STAGES
      ierr = PetscLogStagePop();CHKERRQ(ierr);
#endif
      break;
    }
#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventBegin(petsc_gamg_setup_events[SET2],0,0,0,0);CHKERRQ(ierr);
#endif
    ierr = MatGetSize(Parr[level1], &M, &N);CHKERRQ(ierr); /* N is next M, a loop test variables */
    if (is_last) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_PLIB,"Is last ????????");
    if (N <= pc_gamg->coarse_eq_limit) is_last = PETSC_TRUE;
    if (level1 == pc_gamg->Nlevels-1) is_last = PETSC_TRUE;
    ierr = pc_gamg->ops->createlevel(pc, Aarr[level], bs, &Parr[level1], &Aarr[level1], &nactivepe, NULL, is_last);CHKERRQ(ierr);

#if defined PETSC_GAMG_USE_LOG
    ierr = PetscLogEventEnd(petsc_gamg_setup_events[SET2],0,0,0,0);CHKERRQ(ierr);
#endif
    ierr = MatGetSize(Aarr[level1], &M, &N);CHKERRQ(ierr); /* M is loop test variables */
    ierr = MatGetInfo(Aarr[level1], MAT_GLOBAL_SUM, &info);CHKERRQ(ierr);
    nnztot += info.nz_used;
    ierr = PetscInfo5(pc,"%d) N=%D, n data cols=%d, nnz/row (ave)=%d, %d active pes\n",level1,M,pc_gamg->data_cell_cols,(int)(info.nz_used/(PetscReal)M),nactivepe);CHKERRQ(ierr);

#if (defined PETSC_GAMG_USE_LOG && defined GAMG_STAGES)
    ierr = PetscLogStagePop();CHKERRQ(ierr);
#endif
    /* stop if one node or one proc -- could pull back for singular problems */
    if ( (pc_gamg->data_cell_cols && M/pc_gamg->data_cell_cols < 2) || (!pc_gamg->data_cell_cols && M/bs < 2) ) {
      ierr =  PetscInfo2(pc,"HARD stop of coarsening on level %D.  Grid too small: %D block nodes\n",level,M/bs);CHKERRQ(ierr);
      level++;
      break;
    }
  } /* levels */
  ierr                  = PetscFree(pc_gamg->data);CHKERRQ(ierr);

  ierr = PetscInfo2(pc,"%D levels, grid complexity = %g\n",level+1,nnztot/nnz0);CHKERRQ(ierr);
  pc_gamg->Nlevels = level + 1;
  fine_level       = level;
  ierr             = PCMGSetLevels(pc,pc_gamg->Nlevels,NULL);CHKERRQ(ierr);

  if (pc_gamg->Nlevels > 1) { /* don't setup MG if one level */
    /* set default smoothers & set operators */
    for (lidx = 1, level = pc_gamg->Nlevels-2; lidx <= fine_level; lidx++, level--) {
      KSP smoother;
      PC  subpc;

      ierr = PCMGGetSmoother(pc, lidx, &smoother);CHKERRQ(ierr);
      ierr = KSPGetPC(smoother, &subpc);CHKERRQ(ierr);

      ierr = KSPSetNormType(smoother, KSP_NORM_NONE);CHKERRQ(ierr);
      /* set ops */
      ierr = KSPSetOperators(smoother, Aarr[level], Aarr[level]);CHKERRQ(ierr);
      ierr = PCMGSetInterpolation(pc, lidx, Parr[level+1]);CHKERRQ(ierr);

      /* set defaults */
      ierr = KSPSetType(smoother, KSPCHEBYSHEV);CHKERRQ(ierr);

      /* set blocks for ASM smoother that uses the 'aggregates' */
      if (pc_gamg->use_aggs_in_asm) {
        PetscInt sz;
        IS       *iss;

        sz   = nASMBlocksArr[level];
        iss  = ASMLocalIDsArr[level];
        ierr = PCSetType(subpc, PCASM);CHKERRQ(ierr);
        ierr = PCASMSetOverlap(subpc, 0);CHKERRQ(ierr);
        ierr = PCASMSetType(subpc,PC_ASM_BASIC);CHKERRQ(ierr);
        if (!sz) {
          IS       is;
          ierr = ISCreateGeneral(PETSC_COMM_SELF, 0, NULL, PETSC_COPY_VALUES, &is);CHKERRQ(ierr);
          ierr = PCASMSetLocalSubdomains(subpc, 1, NULL, &is);CHKERRQ(ierr);
          ierr = ISDestroy(&is);CHKERRQ(ierr);
        } else {
          PetscInt kk;
          ierr = PCASMSetLocalSubdomains(subpc, sz, NULL, iss);CHKERRQ(ierr);
          for (kk=0; kk<sz; kk++) {
            ierr = ISDestroy(&iss[kk]);CHKERRQ(ierr);
          }
          ierr = PetscFree(iss);CHKERRQ(ierr);
        }
        ASMLocalIDsArr[level] = NULL;
        nASMBlocksArr[level]  = 0;
      } else {
        ierr = PCSetType(subpc, PCSOR);CHKERRQ(ierr);
      }
    }
    {
      /* coarse grid */
      KSP smoother,*k2; PC subpc,pc2; PetscInt ii,first;
      Mat Lmat = Aarr[(level=pc_gamg->Nlevels-1)]; lidx = 0;
      ierr = PCMGGetSmoother(pc, lidx, &smoother);CHKERRQ(ierr);
      ierr = KSPSetOperators(smoother, Lmat, Lmat);CHKERRQ(ierr);
      if (!pc_gamg->use_parallel_coarse_grid_solver) {
        ierr = KSPSetNormType(smoother, KSP_NORM_NONE);CHKERRQ(ierr);
        ierr = KSPGetPC(smoother, &subpc);CHKERRQ(ierr);
        ierr = PCSetType(subpc, PCBJACOBI);CHKERRQ(ierr);
        ierr = PCSetUp(subpc);CHKERRQ(ierr);
        ierr = PCBJacobiGetSubKSP(subpc,&ii,&first,&k2);CHKERRQ(ierr);
        if (ii != 1) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_PLIB,"ii %D is not one",ii);
        ierr = KSPGetPC(k2[0],&pc2);CHKERRQ(ierr);
        ierr = PCSetType(pc2, PCLU);CHKERRQ(ierr);
        ierr = PCFactorSetShiftType(pc2,MAT_SHIFT_INBLOCKS);CHKERRQ(ierr);
        ierr = KSPSetTolerances(k2[0],PETSC_DEFAULT,PETSC_DEFAULT,PETSC_DEFAULT,1);CHKERRQ(ierr);
        ierr = KSPSetType(k2[0], KSPPREONLY);CHKERRQ(ierr);
        /* This flag gets reset by PCBJacobiGetSubKSP(), but our BJacobi really does the same algorithm everywhere (and in
         * fact, all but one process will have zero dofs), so we reset the flag to avoid having PCView_BJacobi attempt to
         * view every subdomain as though they were different. */
        ((PC_BJacobi*)subpc->data)->same_local_solves = PETSC_TRUE;
      }
    }

    /* should be called in PCSetFromOptions_GAMG(), but cannot be called prior to PCMGSetLevels() */
    ierr = PetscObjectOptionsBegin((PetscObject)pc);CHKERRQ(ierr);
    ierr = PCSetFromOptions_MG(PetscOptionsObject,pc);CHKERRQ(ierr);
    ierr = PetscOptionsEnd();CHKERRQ(ierr);
    ierr = PCMGSetGalerkin(pc,PC_MG_GALERKIN_EXTERNAL);CHKERRQ(ierr);

    /* clean up */
    for (level=1; level<pc_gamg->Nlevels; level++) {
      ierr = MatDestroy(&Parr[level]);CHKERRQ(ierr);
      ierr = MatDestroy(&Aarr[level]);CHKERRQ(ierr);
    }
    ierr = PCSetUp_MG(pc);CHKERRQ(ierr);
  } else {
    KSP smoother;
    ierr = PetscInfo(pc,"One level solver used (system is seen as DD). Using default solver.\n");CHKERRQ(ierr);
    ierr = PCMGGetSmoother(pc, 0, &smoother);CHKERRQ(ierr);
    ierr = KSPSetOperators(smoother, Aarr[0], Aarr[0]);CHKERRQ(ierr);
    ierr = KSPSetType(smoother, KSPPREONLY);CHKERRQ(ierr);
    ierr = PCSetUp_MG(pc);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}

/* ------------------------------------------------------------------------- */
/*
 PCDestroy_GAMG - Destroys the private context for the GAMG preconditioner
   that was created with PCCreate_GAMG().

   Input Parameter:
.  pc - the preconditioner context

   Application Interface Routine: PCDestroy()
*/
PetscErrorCode PCDestroy_GAMG(PC pc)
{
  PetscErrorCode ierr;
  PC_MG          *mg     = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg= (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  ierr = PCReset_GAMG(pc);CHKERRQ(ierr);
  if (pc_gamg->ops->destroy) {
    ierr = (*pc_gamg->ops->destroy)(pc);CHKERRQ(ierr);
  }
  ierr = PetscFree(pc_gamg->ops);CHKERRQ(ierr);
  ierr = PetscFree(pc_gamg->gamg_type_name);CHKERRQ(ierr);
  ierr = PetscFree(pc_gamg);CHKERRQ(ierr);
  ierr = PCDestroy_MG(pc);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetProcEqLim - Set number of equations to aim for per process on the coarse grids via processor reduction.

   Logically Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  n - the number of equations


   Options Database Key:
.  -pc_gamg_process_eq_limit <limit>

   Notes:
    GAMG will reduce the number of MPI processes used directly on the coarse grids so that there are around <limit> equations on each process 
          that has degrees of freedom

   Level: intermediate

.seealso: PCGAMGSetCoarseEqLim()
@*/
PetscErrorCode  PCGAMGSetProcEqLim(PC pc, PetscInt n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetProcEqLim_C",(PC,PetscInt),(pc,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetProcEqLim_GAMG(PC pc, PetscInt n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  if (n>0) pc_gamg->min_eq_proc = n;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetCoarseEqLim - Set maximum number of equations on coarsest grid.

 Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  n - maximum number of equations to aim for

   Options Database Key:
.  -pc_gamg_coarse_eq_limit <limit>

   Notes: For example -pc_gamg_coarse_eq_limit 1000 will stop coarsening once the coarse grid 
     has less than 1000 unknowns.

   Level: intermediate

.seealso: PCGAMGSetProcEqLim()
@*/
PetscErrorCode PCGAMGSetCoarseEqLim(PC pc, PetscInt n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetCoarseEqLim_C",(PC,PetscInt),(pc,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetCoarseEqLim_GAMG(PC pc, PetscInt n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  if (n>0) pc_gamg->coarse_eq_limit = n;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetRepartition - Repartition the degrees of freedom across the processors on the coarser grids

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  n - PETSC_TRUE or PETSC_FALSE

   Options Database Key:
.  -pc_gamg_repartition <true,false>

   Notes:
    this will generally improve the loading balancing of the work on each level

   Level: intermediate

.seealso: ()
@*/
PetscErrorCode PCGAMGSetRepartition(PC pc, PetscBool n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetRepartition_C",(PC,PetscBool),(pc,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetRepartition_GAMG(PC pc, PetscBool n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->repart = n;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetReuseInterpolation - Reuse prolongation when rebuilding algebraic multigrid preconditioner

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  n - PETSC_TRUE or PETSC_FALSE

   Options Database Key:
.  -pc_gamg_reuse_interpolation <true,false>

   Level: intermediate

   Notes:
    this may negatively affect the convergence rate of the method on new matrices if the matrix entries change a great deal, but allows
          rebuilding the preconditioner quicker.

.seealso: ()
@*/
PetscErrorCode PCGAMGSetReuseInterpolation(PC pc, PetscBool n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetReuseInterpolation_C",(PC,PetscBool),(pc,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetReuseInterpolation_GAMG(PC pc, PetscBool n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->reuse_prol = n;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGASMSetUseAggs - Have the PCGAMG smoother on each level use the aggregates defined by the coarsening process as the subdomains for the additive Schwarz preconditioner.

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  flg - PETSC_TRUE to use aggregates, PETSC_FALSE to not

   Options Database Key:
.  -pc_gamg_asm_use_agg

   Level: intermediate

.seealso: ()
@*/
PetscErrorCode PCGAMGASMSetUseAggs(PC pc, PetscBool flg)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGASMSetUseAggs_C",(PC,PetscBool),(pc,flg));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGASMSetUseAggs_GAMG(PC pc, PetscBool flg)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->use_aggs_in_asm = flg;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetUseParallelCoarseGridSolve - allow a parallel coarse grid solver

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  flg - PETSC_TRUE to not force coarse grid onto one processor

   Options Database Key:
.  -pc_gamg_use_parallel_coarse_grid_solver

   Level: intermediate

.seealso: ()
@*/
PetscErrorCode PCGAMGSetUseParallelCoarseGridSolve(PC pc, PetscBool flg)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetUseParallelCoarseGridSolve_C",(PC,PetscBool),(pc,flg));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetUseParallelCoarseGridSolve_GAMG(PC pc, PetscBool flg)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->use_parallel_coarse_grid_solver = flg;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetNlevels -  Sets the maximum number of levels PCGAMG will use

   Not collective on PC

   Input Parameters:
+  pc - the preconditioner
-  n - the maximum number of levels to use

   Options Database Key:
.  -pc_mg_levels

   Level: intermediate

.seealso: ()
@*/
PetscErrorCode PCGAMGSetNlevels(PC pc, PetscInt n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetNlevels_C",(PC,PetscInt),(pc,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetNlevels_GAMG(PC pc, PetscInt n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->Nlevels = n;
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetThreshold - Relative threshold to use for dropping edges in aggregation graph

   Not collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  threshold - the threshold value, 0.0 means keep all nonzero entries in the graph; negative means keep even zero entries in the graph

   Options Database Key:
.  -pc_gamg_threshold <threshold>

   Notes:
    Increasing the threshold decreases the rate of coarsening. Conversely reducing the threshold increases the rate of coarsening (aggressive coarsening) and thereby reduces the complexity of the coarse grids, and generally results in slower solver converge rates. Reducing coarse grid complexity reduced the complexity of Galerkin coarse grid construction considerably.
    Before coarsening or aggregating the graph, GAMG removes small values from the graph with this threshold, and thus reducing the coupling in the graph and a different (perhaps better) coarser set of points.

   Level: intermediate

.seealso: PCGAMGFilterGraph(), PCGAMGSetSquareGraph()
@*/
PetscErrorCode PCGAMGSetThreshold(PC pc, PetscReal v[], PetscInt n)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetThreshold_C",(PC,PetscReal[],PetscInt),(pc,v,n));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetThreshold_GAMG(PC pc, PetscReal v[], PetscInt n)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;
  PetscInt i;
  PetscFunctionBegin;
  for (i=0;i<n;i++) pc_gamg->threshold[i] = v[i];
  do {pc_gamg->threshold[i] = pc_gamg->threshold[i-1]*pc_gamg->threshold_scale;} while (++i<PETSC_GAMG_MAXLEVELS);
  PetscFunctionReturn(0);
}

/*@
   PCGAMGSetThresholdScale - Relative threshold reduction at each level

   Not collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  scale - the threshold value reduction, ussually < 1.0

   Options Database Key:
.  -pc_gamg_threshold_scale <v>

   Level: advanced

.seealso: ()
@*/
PetscErrorCode PCGAMGSetThresholdScale(PC pc, PetscReal v)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetThresholdScale_C",(PC,PetscReal),(pc,v));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetThresholdScale_GAMG(PC pc, PetscReal v)
{
  PC_MG   *mg      = (PC_MG*)pc->data;
  PC_GAMG *pc_gamg = (PC_GAMG*)mg->innerctx;
  PetscFunctionBegin;
  pc_gamg->threshold_scale = v;
  PetscFunctionReturn(0);
}

/*@C
   PCGAMGSetType - Set solution method

   Collective on PC

   Input Parameters:
+  pc - the preconditioner context
-  type - PCGAMGAGG, PCGAMGGEO, or PCGAMGCLASSICAL

   Options Database Key:
.  -pc_gamg_type <agg,geo,classical> - type of algebraic multigrid to apply

   Level: intermediate

.seealso: PCGAMGGetType(), PCGAMG, PCGAMGType
@*/
PetscErrorCode PCGAMGSetType(PC pc, PCGAMGType type)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscTryMethod(pc,"PCGAMGSetType_C",(PC,PCGAMGType),(pc,type));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
   PCGAMGGetType - Get solution method

   Collective on PC

   Input Parameter:
.  pc - the preconditioner context

   Output Parameter:
.  type - the type of algorithm used

   Level: intermediate

.seealso: PCGAMGSetType(), PCGAMGType
@*/
PetscErrorCode PCGAMGGetType(PC pc, PCGAMGType *type)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(pc,PC_CLASSID,1);
  ierr = PetscUseMethod(pc,"PCGAMGGetType_C",(PC,PCGAMGType*),(pc,type));CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGGetType_GAMG(PC pc, PCGAMGType *type)
{
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  *type = pc_gamg->type;
  PetscFunctionReturn(0);
}

static PetscErrorCode PCGAMGSetType_GAMG(PC pc, PCGAMGType type)
{
  PetscErrorCode ierr,(*r)(PC);
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;

  PetscFunctionBegin;
  pc_gamg->type = type;
  ierr = PetscFunctionListFind(GAMGList,type,&r);CHKERRQ(ierr);
  if (!r) SETERRQ1(PETSC_COMM_SELF,PETSC_ERR_ARG_UNKNOWN_TYPE,"Unknown GAMG type %s given",type);
  if (pc_gamg->ops->destroy) {
    ierr = (*pc_gamg->ops->destroy)(pc);CHKERRQ(ierr);
    ierr = PetscMemzero(pc_gamg->ops,sizeof(struct _PCGAMGOps));CHKERRQ(ierr);
    pc_gamg->ops->createlevel = PCGAMGCreateLevel_GAMG;
    /* cleaning up common data in pc_gamg - this should disapear someday */
    pc_gamg->data_cell_cols = 0;
    pc_gamg->data_cell_rows = 0;
    pc_gamg->orig_data_cell_cols = 0;
    pc_gamg->orig_data_cell_rows = 0;
    ierr = PetscFree(pc_gamg->data);CHKERRQ(ierr);
    pc_gamg->data_sz = 0;
  }
  ierr = PetscFree(pc_gamg->gamg_type_name);CHKERRQ(ierr);
  ierr = PetscStrallocpy(type,&pc_gamg->gamg_type_name);CHKERRQ(ierr);
  ierr = (*r)(pc);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*
   PCMGGetGridComplexity - compute coarse grid complexity of MG hierarchy

   Input Parameter:
.  pc - the preconditioner context

   Output Parameter:
.  gc - grid complexity = sum_i(nnz_i) / nnz_0

   Level: advanced
*/
static PetscErrorCode PCMGGetGridComplexity(PC pc, PetscReal *gc)
{
  PetscErrorCode ierr;
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_MG_Levels   **mglevels = mg->levels;
  PetscInt       lev, nnz0 = -1;
  MatInfo        info;
  PetscFunctionBegin;
  if (!mg->nlevels) SETERRQ(PETSC_COMM_SELF,PETSC_ERR_PLIB,"MG has no levels");
  for (lev=0, *gc=0; lev<mg->nlevels; lev++) {
    Mat dB;
    ierr = KSPGetOperators(mglevels[lev]->smoothd,NULL,&dB);CHKERRQ(ierr);
    ierr = MatGetInfo(dB,MAT_GLOBAL_SUM,&info);CHKERRQ(ierr); /* global reduction */
    *gc += (PetscReal)info.nz_used;
    if (lev==mg->nlevels-1) nnz0 = info.nz_used;
  }
  if (nnz0) *gc /= (PetscReal)nnz0;
  else *gc = 0;
  PetscFunctionReturn(0);
}

static PetscErrorCode PCView_GAMG(PC pc,PetscViewer viewer)
{
  PetscErrorCode ierr,i;
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;
  PetscReal       gc=0;
  PetscFunctionBegin;
  ierr = PetscViewerASCIIPrintf(viewer,"    GAMG specific options\n");CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"      Threshold for dropping small values in graph on each level =");CHKERRQ(ierr);
  for (i=0;i<pc_gamg->current_level;i++) {
    ierr = PetscViewerASCIIPrintf(viewer," %g",(double)pc_gamg->threshold[i]);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,"\n");CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"      Threshold scaling factor for each level not specified = %g\n",(double)pc_gamg->threshold_scale);CHKERRQ(ierr);
  if (pc_gamg->use_aggs_in_asm) {
    ierr = PetscViewerASCIIPrintf(viewer,"      Using aggregates from coarsening process to define subdomains for PCASM\n");CHKERRQ(ierr);
  }
  if (pc_gamg->use_parallel_coarse_grid_solver) {
    ierr = PetscViewerASCIIPrintf(viewer,"      Using parallel coarse grid solver (all coarse grid equations not put on one process)\n");CHKERRQ(ierr);
  }
  if (pc_gamg->ops->view) {
    ierr = (*pc_gamg->ops->view)(pc,viewer);CHKERRQ(ierr);
  }
  ierr = PCMGGetGridComplexity(pc,&gc);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,"      Complexity:    grid = %g\n",gc);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PetscErrorCode PCSetFromOptions_GAMG(PetscOptionItems *PetscOptionsObject,PC pc)
{
  PetscErrorCode ierr;
  PC_MG          *mg      = (PC_MG*)pc->data;
  PC_GAMG        *pc_gamg = (PC_GAMG*)mg->innerctx;
  PetscBool      flag;
  MPI_Comm       comm;
  char           prefix[256];
  PetscInt       i,n;
  const char     *pcpre;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)pc,&comm);CHKERRQ(ierr);
  ierr = PetscOptionsHead(PetscOptionsObject,"GAMG options");CHKERRQ(ierr);
  {
    char tname[256];
    ierr = PetscOptionsFList("-pc_gamg_type","Type of AMG method","PCGAMGSetType",GAMGList, pc_gamg->gamg_type_name, tname, sizeof(tname), &flag);CHKERRQ(ierr);
    if (flag) {
      ierr = PCGAMGSetType(pc,tname);CHKERRQ(ierr);
    }
    ierr = PetscOptionsBool("-pc_gamg_repartition","Repartion coarse grids","PCGAMGSetRepartition",pc_gamg->repart,&pc_gamg->repart,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-pc_gamg_reuse_interpolation","Reuse prolongation operator","PCGAMGReuseInterpolation",pc_gamg->reuse_prol,&pc_gamg->reuse_prol,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-pc_gamg_asm_use_agg","Use aggregation aggregates for ASM smoother","PCGAMGASMSetUseAggs",pc_gamg->use_aggs_in_asm,&pc_gamg->use_aggs_in_asm,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsBool("-pc_gamg_use_parallel_coarse_grid_solver","Use parallel coarse grid solver (otherwise put last grid on one process)","PCGAMGSetUseParallelCoarseGridSolve",pc_gamg->use_parallel_coarse_grid_solver,&pc_gamg->use_parallel_coarse_grid_solver,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsInt("-pc_gamg_process_eq_limit","Limit (goal) on number of equations per process on coarse grids","PCGAMGSetProcEqLim",pc_gamg->min_eq_proc,&pc_gamg->min_eq_proc,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsInt("-pc_gamg_coarse_eq_limit","Limit on number of equations for the coarse grid","PCGAMGSetCoarseEqLim",pc_gamg->coarse_eq_limit,&pc_gamg->coarse_eq_limit,NULL);CHKERRQ(ierr);
    ierr = PetscOptionsReal("-pc_gamg_threshold_scale","Scaling of threshold for each level not specified","PCGAMGSetThresholdScale",pc_gamg->threshold_scale,&pc_gamg->threshold_scale,NULL);CHKERRQ(ierr);
    n = PETSC_GAMG_MAXLEVELS;
    ierr = PetscOptionsRealArray("-pc_gamg_threshold","Relative threshold to use for dropping edges in aggregation graph","PCGAMGSetThreshold",pc_gamg->threshold,&n,&flag);CHKERRQ(ierr);
    if (!flag || n < PETSC_GAMG_MAXLEVELS) {
      if (!flag) n = 1;
      i = n;
      do {pc_gamg->threshold[i] = pc_gamg->threshold[i-1]*pc_gamg->threshold_scale;} while (++i<PETSC_GAMG_MAXLEVELS);
    }
    ierr = PetscOptionsInt("-pc_mg_levels","Set number of MG levels","PCGAMGSetNlevels",pc_gamg->Nlevels,&pc_gamg->Nlevels,NULL);CHKERRQ(ierr);

    /* set options for subtype */
    if (pc_gamg->ops->setfromoptions) {ierr = (*pc_gamg->ops->setfromoptions)(PetscOptionsObject,pc);CHKERRQ(ierr);}
  }
  ierr = PCGetOptionsPrefix(pc, &pcpre);CHKERRQ(ierr);
  ierr = PetscSNPrintf(prefix,sizeof(prefix),"%spc_gamg_",pcpre ? pcpre : "");CHKERRQ(ierr);
  ierr = PetscOptionsTail();CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/* -------------------------------------------------------------------------- */
/*MC
     PCGAMG - Geometric algebraic multigrid (AMG) preconditioner

   Options Database Keys:
+   -pc_gamg_type <type> - one of agg, geo, or classical
.   -pc_gamg_repartition  <true,default=false> - repartition the degrees of freedom accross the coarse grids as they are determined
.   -pc_gamg_reuse_interpolation <true,default=false> - when rebuilding the algebraic multigrid preconditioner reuse the previously computed interpolations
.   -pc_gamg_asm_use_agg <true,default=false> - use the aggregates from the coasening process to defined the subdomains on each level for the PCASM smoother
.   -pc_gamg_process_eq_limit <limit, default=50> - GAMG will reduce the number of MPI processes used directly on the coarse grids so that there are around <limit>
                                        equations on each process that has degrees of freedom
.   -pc_gamg_coarse_eq_limit <limit, default=50> - Set maximum number of equations on coarsest grid to aim for.
.   -pc_gamg_threshold[] <thresh,default=0> - Before aggregating the graph GAMG will remove small values from the graph on each level
-   -pc_gamg_threshold_scale <scale,default=1> - Scaling of threshold on each coarser grid if not specified

   Options Database Keys for default Aggregation:
+  -pc_gamg_agg_nsmooths <nsmooth, default=1> - number of smoothing steps to use with smooth aggregation
.  -pc_gamg_sym_graph <true,default=false> - symmetrize the graph before computing the aggregation
-  -pc_gamg_square_graph <n,default=1> - number of levels to square the graph before aggregating it

   Multigrid options:
+  -pc_mg_cycles <v> - v or w, see PCMGSetCycleType()
.  -pc_mg_distinct_smoothup - configure the up and down (pre and post) smoothers separately, see PCMGSetDistinctSmoothUp()
.  -pc_mg_type <multiplicative> - (one of) additive multiplicative full kascade
-  -pc_mg_levels <levels> - Number of levels of multigrid to use.


  Notes:
    In order to obtain good performance for PCGAMG for vector valued problems you must
       Call MatSetBlockSize() to indicate the number of degrees of freedom per grid point
       Call MatSetNearNullSpace() (or PCSetCoordinates() if solving the equations of elasticity) to indicate the near null space of the operator
       See the Users Manual Chapter 4 for more details

  Level: intermediate

.seealso:  PCCreate(), PCSetType(), MatSetBlockSize(), PCMGType, PCSetCoordinates(), MatSetNearNullSpace(), PCGAMGSetType(), PCGAMGAGG, PCGAMGGEO, PCGAMGCLASSICAL, PCGAMGSetProcEqLim(),
           PCGAMGSetCoarseEqLim(), PCGAMGSetRepartition(), PCGAMGRegister(), PCGAMGSetReuseInterpolation(), PCGAMGASMSetUseAggs(), PCGAMGSetUseParallelCoarseGridSolve(), PCGAMGSetNlevels(), PCGAMGSetThreshold(), PCGAMGGetType(), PCGAMGSetReuseInterpolation()
M*/

PETSC_EXTERN PetscErrorCode PCCreate_GAMG(PC pc)
{
  PetscErrorCode ierr,i;
  PC_GAMG        *pc_gamg;
  PC_MG          *mg;

  PetscFunctionBegin;
   /* register AMG type */
  ierr = PCGAMGInitializePackage();CHKERRQ(ierr);

  /* PCGAMG is an inherited class of PCMG. Initialize pc as PCMG */
  ierr = PCSetType(pc, PCMG);CHKERRQ(ierr);
  ierr = PetscObjectChangeTypeName((PetscObject)pc, PCGAMG);CHKERRQ(ierr);

  /* create a supporting struct and attach it to pc */
  ierr         = PetscNewLog(pc,&pc_gamg);CHKERRQ(ierr);
  ierr         = PCMGSetGalerkin(pc,PC_MG_GALERKIN_EXTERNAL);CHKERRQ(ierr);
  mg           = (PC_MG*)pc->data;
  mg->innerctx = pc_gamg;

  ierr = PetscNewLog(pc,&pc_gamg->ops);CHKERRQ(ierr);

  pc_gamg->setup_count = 0;
  /* these should be in subctx but repartitioning needs simple arrays */
  pc_gamg->data_sz = 0;
  pc_gamg->data    = 0;

  /* overwrite the pointers of PCMG by the functions of base class PCGAMG */
  pc->ops->setfromoptions = PCSetFromOptions_GAMG;
  pc->ops->setup          = PCSetUp_GAMG;
  pc->ops->reset          = PCReset_GAMG;
  pc->ops->destroy        = PCDestroy_GAMG;
  mg->view                = PCView_GAMG;

  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCMGGetLevels_C",PCMGGetLevels_MG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCMGSetLevels_C",PCMGSetLevels_MG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetProcEqLim_C",PCGAMGSetProcEqLim_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetCoarseEqLim_C",PCGAMGSetCoarseEqLim_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetRepartition_C",PCGAMGSetRepartition_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetReuseInterpolation_C",PCGAMGSetReuseInterpolation_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGASMSetUseAggs_C",PCGAMGASMSetUseAggs_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetUseParallelCoarseGridSolve_C",PCGAMGSetUseParallelCoarseGridSolve_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetThreshold_C",PCGAMGSetThreshold_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetThresholdScale_C",PCGAMGSetThresholdScale_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetType_C",PCGAMGSetType_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGGetType_C",PCGAMGGetType_GAMG);CHKERRQ(ierr);
  ierr = PetscObjectComposeFunction((PetscObject)pc,"PCGAMGSetNlevels_C",PCGAMGSetNlevels_GAMG);CHKERRQ(ierr);
  pc_gamg->repart           = PETSC_FALSE;
  pc_gamg->reuse_prol       = PETSC_FALSE;
  pc_gamg->use_aggs_in_asm  = PETSC_FALSE;
  pc_gamg->use_parallel_coarse_grid_solver = PETSC_FALSE;
  pc_gamg->min_eq_proc      = 50;
  pc_gamg->coarse_eq_limit  = 50;
  for (i=0;i<PETSC_GAMG_MAXLEVELS;i++) pc_gamg->threshold[i] = 0.;
  pc_gamg->threshold_scale = 1.;
  pc_gamg->Nlevels          = PETSC_GAMG_MAXLEVELS;
  pc_gamg->current_level    = 0; /* don't need to init really */
  pc_gamg->ops->createlevel = PCGAMGCreateLevel_GAMG;

  /* PCSetUp_GAMG assumes that the type has been set, so set it to the default now */
  ierr = PCGAMGSetType(pc,PCGAMGAGG);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
 PCGAMGInitializePackage - This function initializes everything in the PCGAMG package. It is called
    from PCInitializePackage().

 Level: developer

 .seealso: PetscInitialize()
@*/
PetscErrorCode PCGAMGInitializePackage(void)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  if (PCGAMGPackageInitialized) PetscFunctionReturn(0);
  PCGAMGPackageInitialized = PETSC_TRUE;
  ierr = PetscFunctionListAdd(&GAMGList,PCGAMGGEO,PCCreateGAMG_GEO);CHKERRQ(ierr);
  ierr = PetscFunctionListAdd(&GAMGList,PCGAMGAGG,PCCreateGAMG_AGG);CHKERRQ(ierr);
  ierr = PetscFunctionListAdd(&GAMGList,PCGAMGCLASSICAL,PCCreateGAMG_Classical);CHKERRQ(ierr);
  ierr = PetscRegisterFinalize(PCGAMGFinalizePackage);CHKERRQ(ierr);

  /* general events */
  ierr = PetscLogEventRegister("PCGAMGGraph_AGG", 0, &PC_GAMGGraph_AGG);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGGraph_GEO", PC_CLASSID, &PC_GAMGGraph_GEO);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGCoarse_AGG", PC_CLASSID, &PC_GAMGCoarsen_AGG);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGCoarse_GEO", PC_CLASSID, &PC_GAMGCoarsen_GEO);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGProl_AGG", PC_CLASSID, &PC_GAMGProlongator_AGG);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGProl_GEO", PC_CLASSID, &PC_GAMGProlongator_GEO);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("PCGAMGPOpt_AGG", PC_CLASSID, &PC_GAMGOptProlongator_AGG);CHKERRQ(ierr);

#if defined PETSC_GAMG_USE_LOG
  ierr = PetscLogEventRegister("GAMG: createProl", PC_CLASSID, &petsc_gamg_setup_events[SET1]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  Graph", PC_CLASSID, &petsc_gamg_setup_events[GRAPH]);CHKERRQ(ierr);
  /* PetscLogEventRegister("    G.Mat", PC_CLASSID, &petsc_gamg_setup_events[GRAPH_MAT]); */
  /* PetscLogEventRegister("    G.Filter", PC_CLASSID, &petsc_gamg_setup_events[GRAPH_FILTER]); */
  /* PetscLogEventRegister("    G.Square", PC_CLASSID, &petsc_gamg_setup_events[GRAPH_SQR]); */
  ierr = PetscLogEventRegister("  MIS/Agg", PC_CLASSID, &petsc_gamg_setup_events[SET4]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  geo: growSupp", PC_CLASSID, &petsc_gamg_setup_events[SET5]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  geo: triangle", PC_CLASSID, &petsc_gamg_setup_events[SET6]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("    search-set", PC_CLASSID, &petsc_gamg_setup_events[FIND_V]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  SA: col data", PC_CLASSID, &petsc_gamg_setup_events[SET7]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  SA: frmProl0", PC_CLASSID, &petsc_gamg_setup_events[SET8]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  SA: smooth", PC_CLASSID, &petsc_gamg_setup_events[SET9]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("GAMG: partLevel", PC_CLASSID, &petsc_gamg_setup_events[SET2]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  repartition", PC_CLASSID, &petsc_gamg_setup_events[SET12]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  Invert-Sort", PC_CLASSID, &petsc_gamg_setup_events[SET13]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  Move A", PC_CLASSID, &petsc_gamg_setup_events[SET14]);CHKERRQ(ierr);
  ierr = PetscLogEventRegister("  Move P", PC_CLASSID, &petsc_gamg_setup_events[SET15]);CHKERRQ(ierr);

  /* PetscLogEventRegister(" PL move data", PC_CLASSID, &petsc_gamg_setup_events[SET13]); */
  /* PetscLogEventRegister("GAMG: fix", PC_CLASSID, &petsc_gamg_setup_events[SET10]); */
  /* PetscLogEventRegister("GAMG: set levels", PC_CLASSID, &petsc_gamg_setup_events[SET11]); */
  /* create timer stages */
#if defined GAMG_STAGES
  {
    char     str[32];
    PetscInt lidx;
    sprintf(str,"MG Level %d (finest)",0);
    ierr = PetscLogStageRegister(str, &gamg_stages[0]);CHKERRQ(ierr);
    for (lidx=1; lidx<9; lidx++) {
      sprintf(str,"MG Level %d",lidx);
      ierr = PetscLogStageRegister(str, &gamg_stages[lidx]);CHKERRQ(ierr);
    }
  }
#endif
#endif
  PetscFunctionReturn(0);
}

/*@C
 PCGAMGFinalizePackage - This function frees everything from the PCGAMG package. It is
    called from PetscFinalize() automatically.

 Level: developer

 .seealso: PetscFinalize()
@*/
PetscErrorCode PCGAMGFinalizePackage(void)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PCGAMGPackageInitialized = PETSC_FALSE;
  ierr = PetscFunctionListDestroy(&GAMGList);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@C
 PCGAMGRegister - Register a PCGAMG implementation.

 Input Parameters:
 + type - string that will be used as the name of the GAMG type.
 - create - function for creating the gamg context.

  Level: advanced

 .seealso: PCGAMGType, PCGAMG, PCGAMGSetType()
@*/
PetscErrorCode PCGAMGRegister(PCGAMGType type, PetscErrorCode (*create)(PC))
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  ierr = PCGAMGInitializePackage();CHKERRQ(ierr);
  ierr = PetscFunctionListAdd(&GAMGList,type,create);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

