#include <petsc/private/dmmbimpl.h> /*I  "petscdmmoab.h"   I*/

#include <petscdmmoab.h>
#include <MBTagConventions.hpp>
#include <moab/NestedRefine.hpp>
#include <moab/Skinner.hpp>

/*MC
  DMMOAB = "moab" - A DM object that encapsulates an unstructured mesh described by the MOAB mesh database.
                    Direct access to the MOAB Interface and other mesh manipulation related objects are available
                    through public API. Ability to create global and local representation of Vecs containing all
                    unknowns in the interior and shared boundary via a transparent tag-data wrapper is provided
                    along with utility functions to traverse the mesh and assemble a discrete system via
                    field-based/blocked Vec(Get/Set) methods. Input from and output to different formats are
                    available.

  Reference: https://www.mcs.anl.gov/~fathom/moab-docs/html/contents.html

  Level: intermediate

.seealso: DMType, DMMoabCreate(), DMCreate(), DMSetType(), DMMoabCreateMoab()
M*/

/* External function declarations here */
PETSC_EXTERN PetscErrorCode DMCreateInterpolation_Moab(DM dmCoarse, DM dmFine, Mat *interpolation, Vec *scaling);
PETSC_EXTERN PetscErrorCode DMCreateDefaultConstraints_Moab(DM dm);
PETSC_EXTERN PetscErrorCode DMCreateMatrix_Moab(DM dm,  Mat *J);
PETSC_EXTERN PetscErrorCode DMCreateCoordinateDM_Moab(DM dm, DM *cdm);
PETSC_EXTERN PetscErrorCode DMRefine_Moab(DM dm, MPI_Comm comm, DM *dmRefined);
PETSC_EXTERN PetscErrorCode DMCoarsen_Moab(DM dm, MPI_Comm comm, DM *dmCoarsened);
PETSC_EXTERN PetscErrorCode DMRefineHierarchy_Moab(DM dm, PetscInt nlevels, DM dmRefined[]);
PETSC_EXTERN PetscErrorCode DMCoarsenHierarchy_Moab(DM dm, PetscInt nlevels, DM dmCoarsened[]);
PETSC_EXTERN PetscErrorCode DMClone_Moab(DM dm, DM *newdm);
PETSC_EXTERN PetscErrorCode DMCreateGlobalVector_Moab(DM, Vec *);
PETSC_EXTERN PetscErrorCode DMCreateLocalVector_Moab(DM, Vec *);
PETSC_EXTERN PetscErrorCode DMCreateMatrix_Moab(DM dm, Mat *J);
PETSC_EXTERN PetscErrorCode DMGlobalToLocalBegin_Moab(DM, Vec, InsertMode, Vec);
PETSC_EXTERN PetscErrorCode DMGlobalToLocalEnd_Moab(DM, Vec, InsertMode, Vec);
PETSC_EXTERN PetscErrorCode DMLocalToGlobalBegin_Moab(DM, Vec, InsertMode, Vec);
PETSC_EXTERN PetscErrorCode DMLocalToGlobalEnd_Moab(DM, Vec, InsertMode, Vec);


/* Un-implemented routines */
/*
PETSC_EXTERN PetscErrorCode DMCreateDefaultSection_Moab(DM dm);
PETSC_EXTERN PetscErrorCode DMCreateInjection_Moab(DM dmCoarse, DM dmFine, Mat *mat);
PETSC_EXTERN PetscErrorCode DMLoad_Moab(DM dm, PetscViewer viewer);
PETSC_EXTERN PetscErrorCode DMGetDimPoints_Moab(DM dm, PetscInt dim, PetscInt *pStart, PetscInt *pEnd);
PETSC_EXTERN PetscErrorCode DMCreateSubDM_Moab(DM dm, PetscInt numFields, PetscInt fields[], IS *is, DM *subdm);
PETSC_EXTERN PetscErrorCode DMLocatePoints_Moab(DM dm, Vec v, IS *cellIS);
*/

/*@
  DMMoabCreate - Creates a DMMoab object, which encapsulates a moab instance

  Collective

  Input Parameter:
. comm - The communicator for the DMMoab object

  Output Parameter:
. dmb  - The DMMoab object

  Level: beginner

@*/
PetscErrorCode DMMoabCreate(MPI_Comm comm, DM *dmb)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidPointer(dmb, 2);
  ierr = DMCreate(comm, dmb);CHKERRQ(ierr);
  ierr = DMSetType(*dmb, DMMOAB);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

/*@
  DMMoabCreateMoab - Creates a DMMoab object, optionally from an instance and other data

  Collective

  Input Parameter:
+ comm - The communicator for the DMMoab object
. mbiface - (ptr to) the MOAB Instance; if passed in NULL, MOAB instance is created inside PETSc, and destroyed
         along with the DMMoab
. pcomm - (ptr to) a ParallelComm; if NULL, creates one internally for the whole communicator
. ltog_tag - A tag to use to retrieve global id for an entity; if 0, will use GLOBAL_ID_TAG_NAME/tag
- range - If non-NULL, contains range of entities to which DOFs will be assigned

  Output Parameter:
. dmb  - The DMMoab object

  Level: intermediate

@*/
PetscErrorCode DMMoabCreateMoab(MPI_Comm comm, moab::Interface *mbiface, moab::Tag *ltog_tag, moab::Range *range, DM *dmb)
{
  PetscErrorCode ierr;
  moab::ErrorCode merr;
  DM             dmmb;
  DM_Moab        *dmmoab;

  PetscFunctionBegin;
  PetscValidPointer(dmb, 6);

  ierr = DMMoabCreate(comm, &dmmb);CHKERRQ(ierr);
  dmmoab = (DM_Moab*)(dmmb)->data;

  if (!mbiface) {
    dmmoab->mbiface = new moab::Core();
    dmmoab->icreatedinstance = PETSC_TRUE;
  }
  else {
    dmmoab->mbiface = mbiface;
    dmmoab->icreatedinstance = PETSC_FALSE;
  }

  /* by default the fileset = root set. This set stores the hierarchy of entities belonging to current DM */
  dmmoab->fileset = 0;
  dmmoab->hlevel = 0;
  dmmoab->nghostrings = 0;

#ifdef MOAB_HAVE_MPI
  moab::EntityHandle partnset;

  /* Create root sets for each mesh.  Then pass these
      to the load_file functions to be populated. */
  merr = dmmoab->mbiface->create_meshset(moab::MESHSET_SET, partnset); MBERR("Creating partition set failed", merr);

  /* Create the parallel communicator object with the partition handle associated with MOAB */
  dmmoab->pcomm = moab::ParallelComm::get_pcomm(dmmoab->mbiface, partnset, &comm);
#endif

  /* do the remaining initializations for DMMoab */
  dmmoab->bs = 1;
  dmmoab->numFields = 1;
  ierr = PetscMalloc(dmmoab->numFields * sizeof(char*), &dmmoab->fieldNames);CHKERRQ(ierr);
  ierr = PetscStrallocpy("DEFAULT", (char**) &dmmoab->fieldNames[0]);CHKERRQ(ierr);
  dmmoab->rw_dbglevel = 0;
  dmmoab->partition_by_rank = PETSC_FALSE;
  dmmoab->extra_read_options[0] = '\0';
  dmmoab->extra_write_options[0] = '\0';
  dmmoab->read_mode = READ_PART;
  dmmoab->write_mode = WRITE_PART;

  /* set global ID tag handle */
  if (ltog_tag && *ltog_tag) {
    ierr = DMMoabSetLocalToGlobalTag(dmmb, *ltog_tag);CHKERRQ(ierr);
  }
  else {
    merr = dmmoab->mbiface->tag_get_handle(GLOBAL_ID_TAG_NAME, dmmoab->ltog_tag); MBERRNM(merr);
    if (ltog_tag) *ltog_tag = dmmoab->ltog_tag;
  }

  merr = dmmoab->mbiface->tag_get_handle(MATERIAL_SET_TAG_NAME, dmmoab->material_tag); MBERRNM(merr);

  /* set the local range of entities (vertices) of interest */
  if (range) {
    ierr = DMMoabSetLocalVertices(dmmb, range);CHKERRQ(ierr);
  }
  *dmb = dmmb;
  PetscFunctionReturn(0);
}


#ifdef MOAB_HAVE_MPI

/*@
  DMMoabGetParallelComm - Get the ParallelComm used with this DMMoab

  Collective

  Input Parameter:
. dm    - The DMMoab object being set

  Output Parameter:
. pcomm - The ParallelComm for the DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabGetParallelComm(DM dm, moab::ParallelComm **pcomm)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  *pcomm = ((DM_Moab*)(dm)->data)->pcomm;
  PetscFunctionReturn(0);
}

#endif /* MOAB_HAVE_MPI */


/*@
  DMMoabSetInterface - Set the MOAB instance used with this DMMoab

  Collective

  Input Parameter:
+ dm      - The DMMoab object being set
- mbiface - The MOAB instance being set on this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabSetInterface(DM dm, moab::Interface *mbiface)
{
  DM_Moab        *dmmoab = (DM_Moab*)(dm)->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(mbiface, 2);
#ifdef MOAB_HAVE_MPI
  dmmoab->pcomm = NULL;
#endif
  dmmoab->mbiface = mbiface;
  dmmoab->icreatedinstance = PETSC_FALSE;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetInterface - Get the MOAB instance used with this DMMoab

  Collective

  Input Parameter:
. dm      - The DMMoab object being set

  Output Parameter:
. mbiface - The MOAB instance set on this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabGetInterface(DM dm, moab::Interface **mbiface)
{
  PetscErrorCode   ierr;
  static PetscBool cite = PETSC_FALSE;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  ierr = PetscCitationsRegister("@techreport{tautges_moab:_2004,\n  type = {{SAND2004-1592}},\n  title = {{MOAB:} A Mesh-Oriented Database},  institution = {Sandia National Laboratories},\n  author = {Tautges, T. J. and Meyers, R. and Merkley, K. and Stimpson, C. and Ernst, C.},\n  year = {2004},  note = {Report}\n}\n", &cite);CHKERRQ(ierr);
  *mbiface = ((DM_Moab*)dm->data)->mbiface;
  PetscFunctionReturn(0);
}


/*@
  DMMoabSetLocalVertices - Set the entities having DOFs on this DMMoab

  Collective

  Input Parameter:
+ dm    - The DMMoab object being set
- range - The entities treated by this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabSetLocalVertices(DM dm, moab::Range *range)
{
  moab::Range     tmpvtxs;
  DM_Moab        *dmmoab = (DM_Moab*)(dm)->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  dmmoab->vlocal->clear();
  dmmoab->vowned->clear();

  dmmoab->vlocal->insert(range->begin(), range->end());

#ifdef MOAB_HAVE_MPI
  moab::ErrorCode merr;
  /* filter based on parallel status */
  merr = dmmoab->pcomm->filter_pstatus(*dmmoab->vlocal, PSTATUS_NOT_OWNED, PSTATUS_NOT, -1, dmmoab->vowned); MBERRNM(merr);

  /* filter all the non-owned and shared entities out of the list */
  tmpvtxs = moab::subtract(*dmmoab->vlocal, *dmmoab->vowned);
  merr = dmmoab->pcomm->filter_pstatus(tmpvtxs, PSTATUS_INTERFACE, PSTATUS_OR, -1, dmmoab->vghost); MBERRNM(merr);
  tmpvtxs = moab::subtract(tmpvtxs, *dmmoab->vghost);
  *dmmoab->vlocal = moab::subtract(*dmmoab->vlocal, tmpvtxs);
#else
  *dmmoab->vowned = *dmmoab->vlocal;
#endif

  /* compute and cache the sizes of local and ghosted entities */
  dmmoab->nloc = dmmoab->vowned->size();
  dmmoab->nghost = dmmoab->vghost->size();
#ifdef MOAB_HAVE_MPI
  PetscErrorCode  ierr;
  ierr = MPIU_Allreduce(&dmmoab->nloc, &dmmoab->n, 1, MPI_INTEGER, MPI_SUM, ((PetscObject)dm)->comm);CHKERRQ(ierr);
#else
  dmmoab->n = dmmoab->nloc;
#endif
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetAllVertices - Get the entities having DOFs on this DMMoab

  Collective

  Input Parameter:
. dm    - The DMMoab object being set

  Output Parameter:
. owned - The local vertex entities in this DMMoab = (owned+ghosted)

  Level: beginner

@*/
PetscErrorCode DMMoabGetAllVertices(DM dm, moab::Range *local)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (local) *local = *((DM_Moab*)dm->data)->vlocal;
  PetscFunctionReturn(0);
}



/*@
  DMMoabGetLocalVertices - Get the entities having DOFs on this DMMoab

  Collective

  Input Parameter:
. dm    - The DMMoab object being set

  Output Parameters:
+ owned - The owned vertex entities in this DMMoab
- ghost - The ghosted entities (non-owned) stored locally in this partition

  Level: beginner

@*/
PetscErrorCode DMMoabGetLocalVertices(DM dm, const moab::Range **owned, const moab::Range **ghost)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (owned) *owned = ((DM_Moab*)dm->data)->vowned;
  if (ghost) *ghost = ((DM_Moab*)dm->data)->vghost;
  PetscFunctionReturn(0);
}

/*@
  DMMoabGetLocalElements - Get the higher-dimensional entities that are locally owned

  Collective

  Input Parameter:
. dm    - The DMMoab object being set

  Output Parameter:
. range - The entities owned locally

  Level: beginner

@*/
PetscErrorCode DMMoabGetLocalElements(DM dm, const moab::Range **range)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (range) *range = ((DM_Moab*)dm->data)->elocal;
  PetscFunctionReturn(0);
}


/*@
  DMMoabSetLocalElements - Set the entities having DOFs on this DMMoab

  Collective

  Input Parameters:
+ dm    - The DMMoab object being set
- range - The entities treated by this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabSetLocalElements(DM dm, moab::Range *range)
{
  DM_Moab        *dmmoab = (DM_Moab*)(dm)->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  dmmoab->elocal->clear();
  dmmoab->eghost->clear();
  dmmoab->elocal->insert(range->begin(), range->end());
#ifdef MOAB_HAVE_MPI
  moab::ErrorCode merr;
  merr = dmmoab->pcomm->filter_pstatus(*dmmoab->elocal, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
  *dmmoab->eghost = moab::subtract(*range, *dmmoab->elocal);
#endif
  dmmoab->neleloc = dmmoab->elocal->size();
  dmmoab->neleghost = dmmoab->eghost->size();
#ifdef MOAB_HAVE_MPI
  PetscErrorCode  ierr;
  ierr = MPIU_Allreduce(&dmmoab->neleloc, &dmmoab->nele, 1, MPI_INTEGER, MPI_SUM, ((PetscObject)dm)->comm);CHKERRQ(ierr);
  PetscInfo2(dm, "Created %D local and %D global elements.\n", dmmoab->neleloc, dmmoab->nele);
#else
  dmmoab->nele = dmmoab->neleloc;
#endif
  PetscFunctionReturn(0);
}


/*@
  DMMoabSetLocalToGlobalTag - Set the tag used for local to global numbering

  Collective

  Input Parameters:
+ dm      - The DMMoab object being set
- ltogtag - The MOAB tag used for local to global ids

  Level: beginner

@*/
PetscErrorCode DMMoabSetLocalToGlobalTag(DM dm, moab::Tag ltogtag)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  ((DM_Moab*)dm->data)->ltog_tag = ltogtag;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetLocalToGlobalTag - Get the tag used for local to global numbering

  Collective

  Input Parameter:
. dm      - The DMMoab object being set

  Output Parameter:
. ltogtag - The MOAB tag used for local to global ids

  Level: beginner

@*/
PetscErrorCode DMMoabGetLocalToGlobalTag(DM dm, moab::Tag *ltog_tag)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  *ltog_tag = ((DM_Moab*)dm->data)->ltog_tag;
  PetscFunctionReturn(0);
}


/*@
  DMMoabSetBlockSize - Set the block size used with this DMMoab

  Collective

  Input Parameter:
+ dm - The DMMoab object being set
- bs - The block size used with this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabSetBlockSize(DM dm, PetscInt bs)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  ((DM_Moab*)dm->data)->bs = bs;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetBlockSize - Get the block size used with this DMMoab

  Collective

  Input Parameter:
. dm - The DMMoab object being set

  Output Parameter:
. bs - The block size used with this DMMoab

  Level: beginner

@*/
PetscErrorCode DMMoabGetBlockSize(DM dm, PetscInt *bs)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  *bs = ((DM_Moab*)dm->data)->bs;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetSize - Get the global vertex size used with this DMMoab

  Collective on dm

  Input Parameter:
. dm - The DMMoab object being set

  Output Parameter:
+ neg - The number of global elements in the DMMoab instance
- nvg - The number of global vertices in the DMMoab instance

  Level: beginner

@*/
PetscErrorCode DMMoabGetSize(DM dm, PetscInt *neg, PetscInt *nvg)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (neg) *neg = ((DM_Moab*)dm->data)->nele;
  if (nvg) *nvg = ((DM_Moab*)dm->data)->n;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetLocalSize - Get the local and ghosted vertex size used with this DMMoab

  Collective on dm

  Input Parameter:
. dm - The DMMoab object being set

  Output Parameter:
+ nel - The number of owned elements in this processor
. neg - The number of ghosted elements in this processor
. nvl - The number of owned vertices in this processor
- nvg - The number of ghosted vertices in this processor

  Level: beginner

@*/
PetscErrorCode DMMoabGetLocalSize(DM dm, PetscInt *nel, PetscInt *neg, PetscInt *nvl, PetscInt *nvg)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (nel) *nel = ((DM_Moab*)dm->data)->neleloc;
  if (neg) *neg = ((DM_Moab*)dm->data)->neleghost;
  if (nvl) *nvl = ((DM_Moab*)dm->data)->nloc;
  if (nvg) *nvg = ((DM_Moab*)dm->data)->nghost;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetOffset - Get the local offset for the global vector

  Collective

  Input Parameter:
. dm - The DMMoab object being set

  Output Parameter:
. offset - The local offset for the global vector

  Level: beginner

@*/
PetscErrorCode DMMoabGetOffset(DM dm, PetscInt *offset)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  *offset = ((DM_Moab*)dm->data)->vstart;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetDimension - Get the dimension of the DM Mesh

  Collective

  Input Parameter:
. dm - The DMMoab object

  Output Parameter:
. dim - The dimension of DM

  Level: beginner

@*/
PetscErrorCode DMMoabGetDimension(DM dm, PetscInt *dim)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  *dim = ((DM_Moab*)dm->data)->dim;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetHierarchyLevel - Get the current level of the mesh hierarchy
  generated through uniform refinement.

  Collective on dm

  Input Parameter:
. dm - The DMMoab object being set

  Output Parameter:
. nvg - The current mesh hierarchy level

  Level: beginner

@*/
PetscErrorCode DMMoabGetHierarchyLevel(DM dm, PetscInt *nlevel)
{
  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (nlevel) *nlevel = ((DM_Moab*)dm->data)->hlevel;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetMaterialBlock - Get the material ID corresponding to the current entity of the DM Mesh

  Collective

  Input Parameter:
+ dm - The DMMoab object
- ehandle - The element entity handle

  Output Parameter:
. mat - The material ID for the current entity

  Level: beginner

@*/
PetscErrorCode DMMoabGetMaterialBlock(DM dm, const moab::EntityHandle ehandle, PetscInt *mat)
{
  DM_Moab         *dmmoab;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  if (*mat) {
    dmmoab = (DM_Moab*)(dm)->data;
    *mat = dmmoab->materials[dmmoab->elocal->index(ehandle)];
  }
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetVertexCoordinates - Get the coordinates corresponding to the requested vertex entities

  Collective

  Input Parameter:
+ dm - The DMMoab object
. nconn - Number of entities whose coordinates are needed
- conn - The vertex entity handles

  Output Parameter:
. vpos - The coordinates of the requested vertex entities

  Level: beginner

.seealso: DMMoabGetVertexConnectivity()
@*/
PetscErrorCode DMMoabGetVertexCoordinates(DM dm, PetscInt nconn, const moab::EntityHandle *conn, PetscReal *vpos)
{
  DM_Moab         *dmmoab;
  moab::ErrorCode merr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(conn, 3);
  PetscValidPointer(vpos, 4);
  dmmoab = (DM_Moab*)(dm)->data;

  /* Get connectivity information in MOAB canonical ordering */
  if (dmmoab->hlevel) {
    merr = dmmoab->hierarchy->get_coordinates(const_cast<moab::EntityHandle*>(conn), nconn, dmmoab->hlevel, vpos);MBERRNM(merr);
  }
  else {
    merr = dmmoab->mbiface->get_coords(conn, nconn, vpos);MBERRNM(merr);
  }
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetVertexConnectivity - Get the vertex adjacency for the given entity

  Collective

  Input Parameter:
+ dm - The DMMoab object
- vhandle - Vertex entity handle

  Output Parameter:
+ nconn - Number of entities whose coordinates are needed
- conn - The vertex entity handles

  Level: beginner

.seealso: DMMoabGetVertexCoordinates(), DMMoabRestoreVertexConnectivity()
@*/
PetscErrorCode DMMoabGetVertexConnectivity(DM dm, moab::EntityHandle vhandle, PetscInt* nconn, moab::EntityHandle **conn)
{
  DM_Moab        *dmmoab;
  std::vector<moab::EntityHandle> adj_entities, connect;
  PetscErrorCode  ierr;
  moab::ErrorCode merr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(conn, 4);
  dmmoab = (DM_Moab*)(dm)->data;

  /* Get connectivity information in MOAB canonical ordering */
  merr = dmmoab->mbiface->get_adjacencies(&vhandle, 1, 1, true, adj_entities, moab::Interface::UNION); MBERRNM(merr);
  merr = dmmoab->mbiface->get_connectivity(&adj_entities[0], adj_entities.size(), connect); MBERRNM(merr);

  if (conn) {
    ierr = PetscMalloc(sizeof(moab::EntityHandle) * connect.size(), conn);CHKERRQ(ierr);
    ierr = PetscArraycpy(*conn, &connect[0], connect.size());CHKERRQ(ierr);
  }
  if (nconn) *nconn = connect.size();
  PetscFunctionReturn(0);
}


/*@
  DMMoabRestoreVertexConnectivity - Restore the vertex connectivity for the given entity

  Collective

  Input Parameter:
+ dm - The DMMoab object
. vhandle - Vertex entity handle
. nconn - Number of entities whose coordinates are needed
- conn - The vertex entity handles

  Level: beginner

.seealso: DMMoabGetVertexCoordinates(), DMMoabGetVertexConnectivity()
@*/
PetscErrorCode DMMoabRestoreVertexConnectivity(DM dm, moab::EntityHandle ehandle, PetscInt* nconn, moab::EntityHandle **conn)
{
  PetscErrorCode  ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(conn, 4);

  if (conn) {
    ierr = PetscFree(*conn);CHKERRQ(ierr);
  }
  if (nconn) *nconn = 0;
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetElementConnectivity - Get the vertex adjacency for the given entity

  Collective

  Input Parameter:
+ dm - The DMMoab object
- ehandle - Vertex entity handle

  Output Parameter:
+ nconn - Number of entities whose coordinates are needed
- conn - The vertex entity handles

  Level: beginner

.seealso: DMMoabGetVertexCoordinates(), DMMoabGetVertexConnectivity(), DMMoabRestoreVertexConnectivity()
@*/
PetscErrorCode DMMoabGetElementConnectivity(DM dm, moab::EntityHandle ehandle, PetscInt* nconn, const moab::EntityHandle **conn)
{
  DM_Moab        *dmmoab;
  const moab::EntityHandle *connect;
  std::vector<moab::EntityHandle> vconn;
  moab::ErrorCode merr;
  PetscInt nnodes;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(conn, 4);
  dmmoab = (DM_Moab*)(dm)->data;

  /* Get connectivity information in MOAB canonical ordering */
  merr = dmmoab->mbiface->get_connectivity(ehandle, connect, nnodes); MBERRNM(merr);
  if (conn) *conn = connect;
  if (nconn) *nconn = nnodes;
  PetscFunctionReturn(0);
}


/*@
  DMMoabIsEntityOnBoundary - Check whether a given entity is on the boundary (vertex, edge, face, element)

  Collective

  Input Parameter:
+ dm - The DMMoab object
- ent - Entity handle

  Output Parameter:
. ent_on_boundary - PETSC_TRUE if entity on boundary; PETSC_FALSE otherwise

  Level: beginner

.seealso: DMMoabCheckBoundaryVertices()
@*/
PetscErrorCode DMMoabIsEntityOnBoundary(DM dm, const moab::EntityHandle ent, PetscBool* ent_on_boundary)
{
  moab::EntityType etype;
  DM_Moab         *dmmoab;
  PetscInt         edim;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(ent_on_boundary, 3);
  dmmoab = (DM_Moab*)(dm)->data;

  /* get the entity type and handle accordingly */
  etype = dmmoab->mbiface->type_from_handle(ent);
  if (etype >= moab::MBPOLYHEDRON) SETERRQ1(PETSC_COMM_WORLD, PETSC_ERR_ARG_OUTOFRANGE, "Entity type on the boundary skin is invalid. EntityType = %D\n", etype);

  /* get the entity dimension */
  edim = dmmoab->mbiface->dimension_from_handle(ent);

  *ent_on_boundary = PETSC_FALSE;
  if (etype == moab::MBVERTEX && edim == 0) {
    *ent_on_boundary = ((dmmoab->bndyvtx->index(ent) >= 0) ? PETSC_TRUE : PETSC_FALSE);
  }
  else {
    if (edim == dmmoab->dim) {  /* check the higher-dimensional elements first */
      if (dmmoab->bndyelems->index(ent) >= 0) *ent_on_boundary = PETSC_TRUE;
    }
    else {                      /* next check the lower-dimensional faces */
      if (dmmoab->bndyfaces->index(ent) >= 0) *ent_on_boundary = PETSC_TRUE;
    }
  }
  PetscFunctionReturn(0);
}


/*@
  DMMoabIsEntityOnBoundary - Check whether a given entity is on the boundary (vertex, edge, face, element)

  Input Parameter:
+ dm - The DMMoab object
. nconn - Number of handles
- cnt - Array of entity handles

  Output Parameter:
. isbdvtx - Array of boundary markers - PETSC_TRUE if entity on boundary; PETSC_FALSE otherwise

  Level: beginner

.seealso: DMMoabIsEntityOnBoundary()
@*/
PetscErrorCode DMMoabCheckBoundaryVertices(DM dm, PetscInt nconn, const moab::EntityHandle *cnt, PetscBool* isbdvtx)
{
  DM_Moab        *dmmoab;
  PetscInt       i;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(cnt, 3);
  PetscValidPointer(isbdvtx, 4);
  dmmoab = (DM_Moab*)(dm)->data;

  for (i = 0; i < nconn; ++i) {
    isbdvtx[i] = (dmmoab->bndyvtx->index(cnt[i]) >= 0 ? PETSC_TRUE : PETSC_FALSE);
  }
  PetscFunctionReturn(0);
}


/*@
  DMMoabGetBoundaryMarkers - Return references to the vertices, faces, elements on the boundary

  Input Parameter:
. dm - The DMMoab object

  Output Parameter:
+ bdvtx - Boundary vertices
. bdelems - Boundary elements
- bdfaces - Boundary faces

  Level: beginner

.seealso: DMMoabCheckBoundaryVertices(), DMMoabIsEntityOnBoundary()
@*/
PetscErrorCode DMMoabGetBoundaryMarkers(DM dm, const moab::Range **bdvtx, const moab::Range** bdelems, const moab::Range** bdfaces)
{
  DM_Moab        *dmmoab;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  dmmoab = (DM_Moab*)(dm)->data;

  if (bdvtx)  *bdvtx = dmmoab->bndyvtx;
  if (bdfaces)  *bdfaces = dmmoab->bndyfaces;
  if (bdelems)  *bdfaces = dmmoab->bndyelems;
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMDestroy_Moab(DM dm)
{
  PetscErrorCode  ierr;
  PetscInt        i;
  moab::ErrorCode merr;
  DM_Moab        *dmmoab = (DM_Moab*)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);

  dmmoab->refct--;
  if (!dmmoab->refct) {
    delete dmmoab->vlocal;
    delete dmmoab->vowned;
    delete dmmoab->vghost;
    delete dmmoab->elocal;
    delete dmmoab->eghost;
    delete dmmoab->bndyvtx;
    delete dmmoab->bndyfaces;
    delete dmmoab->bndyelems;

    ierr = PetscFree(dmmoab->gsindices);CHKERRQ(ierr);
    ierr = PetscFree2(dmmoab->gidmap, dmmoab->lidmap);CHKERRQ(ierr);
    ierr = PetscFree(dmmoab->dfill);CHKERRQ(ierr);
    ierr = PetscFree(dmmoab->ofill);CHKERRQ(ierr);
    ierr = PetscFree(dmmoab->materials);CHKERRQ(ierr);
    if (dmmoab->fieldNames) {
      for (i = 0; i < dmmoab->numFields; i++) {
        ierr = PetscFree(dmmoab->fieldNames[i]);CHKERRQ(ierr);
      }
      ierr = PetscFree(dmmoab->fieldNames);CHKERRQ(ierr);
    }

    if (dmmoab->nhlevels) {
      ierr = PetscFree(dmmoab->hsets);CHKERRQ(ierr);
      dmmoab->nhlevels = 0;
      if (!dmmoab->hlevel && dmmoab->icreatedinstance) delete dmmoab->hierarchy;
      dmmoab->hierarchy = NULL;
    }

    if (dmmoab->icreatedinstance) {
      delete dmmoab->pcomm;
      merr = dmmoab->mbiface->delete_mesh(); MBERRNM(merr);
      delete dmmoab->mbiface;
    }
    dmmoab->mbiface = NULL;
#ifdef MOAB_HAVE_MPI
    dmmoab->pcomm = NULL;
#endif
    ierr = VecScatterDestroy(&dmmoab->ltog_sendrecv);CHKERRQ(ierr);
    ierr = ISLocalToGlobalMappingDestroy(&dmmoab->ltog_map);CHKERRQ(ierr);
    ierr = PetscFree(dm->data);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMSetFromOptions_Moab(PetscOptionItems *PetscOptionsObject, DM dm)
{
  PetscErrorCode ierr;
  DM_Moab        *dmmoab = (DM_Moab*)dm->data;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  ierr = PetscOptionsHead(PetscOptionsObject, "DMMoab Options");CHKERRQ(ierr);
  ierr  = PetscOptionsBoundedInt("-dm_moab_rw_dbg", "The verbosity level for reading and writing MOAB meshes", "DMView", dmmoab->rw_dbglevel, &dmmoab->rw_dbglevel, NULL,0);CHKERRQ(ierr);
  ierr  = PetscOptionsBool("-dm_moab_partiton_by_rank", "Use partition by rank when reading MOAB meshes from file", "DMView", dmmoab->partition_by_rank, &dmmoab->partition_by_rank, NULL);CHKERRQ(ierr);
  /* TODO: typically, the read options are needed before a DM is completely created and available in which case, the options wont be available ?? */
  ierr  = PetscOptionsString("-dm_moab_read_opts", "Extra options to enable MOAB reader to load DM from file", "DMView", dmmoab->extra_read_options, dmmoab->extra_read_options, PETSC_MAX_PATH_LEN, NULL);CHKERRQ(ierr);
  ierr  = PetscOptionsString("-dm_moab_write_opts", "Extra options to enable MOAB writer to serialize DM to file", "DMView", dmmoab->extra_write_options, dmmoab->extra_write_options, PETSC_MAX_PATH_LEN, NULL);CHKERRQ(ierr);
  ierr  = PetscOptionsEnum("-dm_moab_read_mode", "MOAB parallel read mode", "DMView", MoabReadModes, (PetscEnum)dmmoab->read_mode, (PetscEnum*)&dmmoab->read_mode, NULL);CHKERRQ(ierr);
  ierr  = PetscOptionsEnum("-dm_moab_write_mode", "MOAB parallel write mode", "DMView", MoabWriteModes, (PetscEnum)dmmoab->write_mode, (PetscEnum*)&dmmoab->write_mode, NULL);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMSetUp_Moab(DM dm)
{
  PetscErrorCode          ierr;
  moab::ErrorCode         merr;
  Vec                     local, global;
  IS                      from, to;
  moab::Range::iterator   iter;
  PetscInt                i, j, f, bs, vent, totsize, *lgmap;
  DM_Moab                *dmmoab = (DM_Moab*)dm->data;
  moab::Range             adjs;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  /* Get the local and shared vertices and cache it */
  if (dmmoab->mbiface == NULL) SETERRQ(PETSC_COMM_WORLD, PETSC_ERR_ORDER, "Set the MOAB Interface before calling SetUp.");
#ifdef MOAB_HAVE_MPI
  if (dmmoab->pcomm == NULL) SETERRQ(PETSC_COMM_WORLD, PETSC_ERR_ORDER, "Set the MOAB ParallelComm object before calling SetUp.");
#endif

  /* Get the entities recursively in the current part of the mesh, if user did not set the local vertices explicitly */
  if (dmmoab->vlocal->empty())
  {
    //merr = dmmoab->mbiface->get_entities_by_type(dmmoab->fileset,moab::MBVERTEX,*dmmoab->vlocal,true);MBERRNM(merr);
    merr = dmmoab->mbiface->get_entities_by_dimension(dmmoab->fileset, 0, *dmmoab->vlocal, false); MBERRNM(merr);

#ifdef MOAB_HAVE_MPI
    /* filter based on parallel status */
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->vlocal, PSTATUS_NOT_OWNED, PSTATUS_NOT, -1, dmmoab->vowned); MBERRNM(merr);

    /* filter all the non-owned and shared entities out of the list */
    // *dmmoab->vghost = moab::subtract(*dmmoab->vlocal, *dmmoab->vowned);
    adjs = moab::subtract(*dmmoab->vlocal, *dmmoab->vowned);
    merr = dmmoab->pcomm->filter_pstatus(adjs, PSTATUS_GHOST | PSTATUS_INTERFACE, PSTATUS_OR, -1, dmmoab->vghost); MBERRNM(merr);
    adjs = moab::subtract(adjs, *dmmoab->vghost);
    *dmmoab->vlocal = moab::subtract(*dmmoab->vlocal, adjs);
#else
    *dmmoab->vowned = *dmmoab->vlocal;
#endif

    /* compute and cache the sizes of local and ghosted entities */
    dmmoab->nloc = dmmoab->vowned->size();
    dmmoab->nghost = dmmoab->vghost->size();

#ifdef MOAB_HAVE_MPI
    ierr = MPIU_Allreduce(&dmmoab->nloc, &dmmoab->n, 1, MPI_INTEGER, MPI_SUM, ((PetscObject)dm)->comm);CHKERRQ(ierr);
    PetscInfo4(NULL, "Filset ID: %u, Vertices: local - %D, owned - %D, ghosted - %D.\n", dmmoab->fileset, dmmoab->vlocal->size(), dmmoab->nloc, dmmoab->nghost);
#else
    dmmoab->n = dmmoab->nloc;
#endif
  }

  {
    /* get the information about the local elements in the mesh */
    dmmoab->eghost->clear();

    /* first decipher the leading dimension */
    for (i = 3; i > 0; i--) {
      dmmoab->elocal->clear();
      merr = dmmoab->mbiface->get_entities_by_dimension(dmmoab->fileset, i, *dmmoab->elocal, false); MBERRNM(merr);

      /* store the current mesh dimension */
      if (dmmoab->elocal->size()) {
        dmmoab->dim = i;
        break;
      }
    }

    ierr = DMSetDimension(dm, dmmoab->dim);CHKERRQ(ierr);

#ifdef MOAB_HAVE_MPI
    /* filter the ghosted and owned element list */
    *dmmoab->eghost = *dmmoab->elocal;
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->elocal, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
    *dmmoab->eghost = moab::subtract(*dmmoab->eghost, *dmmoab->elocal);
#endif

    dmmoab->neleloc = dmmoab->elocal->size();
    dmmoab->neleghost = dmmoab->eghost->size();

#ifdef MOAB_HAVE_MPI
    ierr = MPIU_Allreduce(&dmmoab->neleloc, &dmmoab->nele, 1, MPI_INTEGER, MPI_SUM, ((PetscObject)dm)->comm);CHKERRQ(ierr);
    PetscInfo3(NULL, "%d-dim elements: owned - %D, ghosted - %D.\n", dmmoab->dim, dmmoab->neleloc, dmmoab->neleghost);
#else
    dmmoab->nele = dmmoab->neleloc;
#endif
  }

  bs = dmmoab->bs;
  if (!dmmoab->ltog_tag) {
    /* Get the global ID tag. The global ID tag is applied to each
       vertex. It acts as an global identifier which MOAB uses to
       assemble the individual pieces of the mesh */
    merr = dmmoab->mbiface->tag_get_handle(GLOBAL_ID_TAG_NAME, dmmoab->ltog_tag); MBERRNM(merr);
  }

  totsize = dmmoab->vlocal->size();
  if (totsize != dmmoab->nloc + dmmoab->nghost) SETERRQ2(PETSC_COMM_SELF, PETSC_ERR_ARG_OUTOFRANGE, "Mismatch between local and owned+ghost vertices. %D != %D.", totsize, dmmoab->nloc + dmmoab->nghost);
  ierr = PetscCalloc1(totsize, &dmmoab->gsindices);CHKERRQ(ierr);
  {
    /* first get the local indices */
    merr = dmmoab->mbiface->tag_get_data(dmmoab->ltog_tag, *dmmoab->vowned, &dmmoab->gsindices[0]); MBERRNM(merr);
    if (dmmoab->nghost) {  /* next get the ghosted indices */
      merr = dmmoab->mbiface->tag_get_data(dmmoab->ltog_tag, *dmmoab->vghost, &dmmoab->gsindices[dmmoab->nloc]); MBERRNM(merr);
    }

    /* find out the local and global minima of GLOBAL_ID */
    dmmoab->lminmax[0] = dmmoab->lminmax[1] = dmmoab->gsindices[0];
    for (i = 0; i < totsize; ++i) {
      if (dmmoab->lminmax[0] > dmmoab->gsindices[i]) dmmoab->lminmax[0] = dmmoab->gsindices[i];
      if (dmmoab->lminmax[1] < dmmoab->gsindices[i]) dmmoab->lminmax[1] = dmmoab->gsindices[i];
    }

    ierr = MPIU_Allreduce(&dmmoab->lminmax[0], &dmmoab->gminmax[0], 1, MPI_INT, MPI_MIN, ((PetscObject)dm)->comm);CHKERRQ(ierr);
    ierr = MPIU_Allreduce(&dmmoab->lminmax[1], &dmmoab->gminmax[1], 1, MPI_INT, MPI_MAX, ((PetscObject)dm)->comm);CHKERRQ(ierr);

    /* set the GID map */
    for (i = 0; i < totsize; ++i) {
      dmmoab->gsindices[i] -= dmmoab->gminmax[0]; /* zero based index needed for IS */

    }
    dmmoab->lminmax[0] -= dmmoab->gminmax[0];
    dmmoab->lminmax[1] -= dmmoab->gminmax[0];

    PetscInfo4(NULL, "GLOBAL_ID: Local [min, max] - [%D, %D], Global [min, max] - [%D, %D]\n", dmmoab->lminmax[0], dmmoab->lminmax[1], dmmoab->gminmax[0], dmmoab->gminmax[1]);
  }
  if (!(dmmoab->bs == dmmoab->numFields || dmmoab->bs == 1)) SETERRQ3(PETSC_COMM_SELF, PETSC_ERR_ARG_WRONG, "Mismatch between block size and number of component fields. %D != 1 OR %D != %D.", dmmoab->bs, dmmoab->bs, dmmoab->numFields);

  {
    dmmoab->seqstart = dmmoab->mbiface->id_from_handle(dmmoab->vlocal->front());
    dmmoab->seqend = dmmoab->mbiface->id_from_handle(dmmoab->vlocal->back());
    PetscInfo2(NULL, "SEQUENCE: Local [min, max] - [%D, %D]\n", dmmoab->seqstart, dmmoab->seqend);

    ierr = PetscMalloc2(dmmoab->seqend - dmmoab->seqstart + 1, &dmmoab->gidmap, dmmoab->seqend - dmmoab->seqstart + 1, &dmmoab->lidmap);CHKERRQ(ierr);
    ierr = PetscMalloc1(totsize * dmmoab->numFields, &lgmap);CHKERRQ(ierr);

    i = j = 0;
    /* set the owned vertex data first */
    for (moab::Range::iterator iter = dmmoab->vowned->begin(); iter != dmmoab->vowned->end(); iter++, i++) {
      vent = dmmoab->mbiface->id_from_handle(*iter) - dmmoab->seqstart;
      dmmoab->gidmap[vent] = dmmoab->gsindices[i];
      dmmoab->lidmap[vent] = i;
      for (f = 0; f < dmmoab->numFields; f++, j++) {
        lgmap[j] = (bs > 1 ? dmmoab->gsindices[i] * dmmoab->numFields + f : totsize * f + dmmoab->gsindices[i]);
      }
    }
    /* next arrange all the ghosted data information */
    for (moab::Range::iterator iter = dmmoab->vghost->begin(); iter != dmmoab->vghost->end(); iter++, i++) {
      vent = dmmoab->mbiface->id_from_handle(*iter) - dmmoab->seqstart;
      dmmoab->gidmap[vent] = dmmoab->gsindices[i];
      dmmoab->lidmap[vent] = i;
      for (f = 0; f < dmmoab->numFields; f++, j++) {
        lgmap[j] = (bs > 1 ? dmmoab->gsindices[i] * dmmoab->numFields + f : totsize * f + dmmoab->gsindices[i]);
      }
    }

    /* We need to create the Global to Local Vector Scatter Contexts
       1) First create a local and global vector
       2) Create a local and global IS
       3) Create VecScatter and LtoGMapping objects
       4) Cleanup the IS and Vec objects
    */
    ierr = DMCreateGlobalVector(dm, &global);CHKERRQ(ierr);
    ierr = DMCreateLocalVector(dm, &local);CHKERRQ(ierr);

    ierr = VecGetOwnershipRange(global, &dmmoab->vstart, &dmmoab->vend);CHKERRQ(ierr);

    /* global to local must retrieve ghost points */
    ierr = ISCreateStride(((PetscObject)dm)->comm, dmmoab->nloc * dmmoab->numFields, dmmoab->vstart, 1, &from);CHKERRQ(ierr);
    ierr = ISSetBlockSize(from, bs);CHKERRQ(ierr);

    ierr = ISCreateGeneral(((PetscObject)dm)->comm, dmmoab->nloc * dmmoab->numFields, &lgmap[0], PETSC_COPY_VALUES, &to);CHKERRQ(ierr);
    ierr = ISSetBlockSize(to, bs);CHKERRQ(ierr);

    if (!dmmoab->ltog_map) {
      /* create to the local to global mapping for vectors in order to use VecSetValuesLocal */
      ierr = ISLocalToGlobalMappingCreate(((PetscObject)dm)->comm, dmmoab->bs, totsize * dmmoab->numFields, lgmap,
                                          PETSC_COPY_VALUES, &dmmoab->ltog_map);CHKERRQ(ierr);
    }

    /* now create the scatter object from local to global vector */
    ierr = VecScatterCreate(local, from, global, to, &dmmoab->ltog_sendrecv);CHKERRQ(ierr);

    /* clean up IS, Vec */
    ierr = PetscFree(lgmap);CHKERRQ(ierr);
    ierr = ISDestroy(&from);CHKERRQ(ierr);
    ierr = ISDestroy(&to);CHKERRQ(ierr);
    ierr = VecDestroy(&local);CHKERRQ(ierr);
    ierr = VecDestroy(&global);CHKERRQ(ierr);
  }

  dmmoab->bndyvtx = new moab::Range();
  dmmoab->bndyfaces = new moab::Range();
  dmmoab->bndyelems = new moab::Range();
  /* skin the boundary and store nodes */
  if (!dmmoab->hlevel) {
    /* get the skin vertices of boundary faces for the current partition and then filter
       the local, boundary faces, vertices and elements alone via PSTATUS flags;
       this should not give us any ghosted boundary, but if user needs such a functionality
       it would be easy to add it based on the find_skin query below */
    moab::Skinner skinner(dmmoab->mbiface);

    /* get the entities on the skin - only the faces */
    merr = skinner.find_skin(dmmoab->fileset, *dmmoab->elocal, false, *dmmoab->bndyfaces, NULL, true, true, false); MBERRNM(merr); // 'false' param indicates we want faces back, not vertices

#ifdef MOAB_HAVE_MPI
    /* filter all the non-owned and shared entities out of the list */
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->bndyfaces, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->bndyfaces, PSTATUS_INTERFACE, PSTATUS_NOT); MBERRNM(merr);
#endif

    /* get all the nodes via connectivity and the parent elements via adjacency information */
    merr = dmmoab->mbiface->get_connectivity(*dmmoab->bndyfaces, *dmmoab->bndyvtx, false); MBERRNM(ierr);
    merr = dmmoab->mbiface->get_adjacencies(*dmmoab->bndyvtx, dmmoab->dim, false, *dmmoab->bndyelems, moab::Interface::UNION); MBERRNM(ierr);
  }
  else {
    /* Let us query the hierarchy manager and get the results directly for this level */
    for (moab::Range::iterator iter = dmmoab->elocal->begin(); iter != dmmoab->elocal->end(); iter++) {
      moab::EntityHandle elemHandle = *iter;
      if (dmmoab->hierarchy->is_entity_on_boundary(elemHandle)) {
        dmmoab->bndyelems->insert(elemHandle);
        /* For this boundary element, query the vertices and add them to the list */
        std::vector<moab::EntityHandle> connect;
        merr = dmmoab->hierarchy->get_connectivity(elemHandle, dmmoab->hlevel, connect); MBERRNM(ierr);
        for (unsigned iv=0; iv < connect.size(); ++iv)
          if (dmmoab->hierarchy->is_entity_on_boundary(connect[iv]))
            dmmoab->bndyvtx->insert(connect[iv]);
        /* Next, let us query the boundary faces and add them also to the list */
        std::vector<moab::EntityHandle> faces;
        merr = dmmoab->hierarchy->get_adjacencies(elemHandle, dmmoab->dim-1, faces); MBERRNM(ierr);
        for (unsigned ifa=0; ifa < faces.size(); ++ifa)
          if (dmmoab->hierarchy->is_entity_on_boundary(faces[ifa]))
            dmmoab->bndyfaces->insert(faces[ifa]);
      }
    }
#ifdef MOAB_HAVE_MPI
    /* filter all the non-owned and shared entities out of the list */
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->bndyvtx,   PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->bndyfaces, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
    merr = dmmoab->pcomm->filter_pstatus(*dmmoab->bndyelems, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
#endif

  }
  PetscInfo3(NULL, "Found %D boundary vertices, %D boundary faces and %D boundary elements.\n", dmmoab->bndyvtx->size(), dmmoab->bndyfaces->size(), dmmoab->bndyelems->size());

  /* Get the material sets and populate the data for all locally owned elements */
  {
    ierr = PetscCalloc1(dmmoab->elocal->size(), &dmmoab->materials);CHKERRQ(ierr);
    /* Get the count of entities of particular type from dmmoab->elocal
       -- Then, for each non-zero type, loop through and query the fileset to get the material tag data */
    moab::Range msets;
    merr = dmmoab->mbiface->get_entities_by_type_and_tag(dmmoab->fileset, moab::MBENTITYSET, &dmmoab->material_tag, NULL, 1, msets, moab::Interface::UNION);MB_CHK_ERR(merr);
    if (msets.size() == 0) {
      PetscInfo(NULL, "No material sets found in the fileset.");
    }

    for (unsigned i=0; i < msets.size(); ++i) {
      moab::Range msetelems;
      merr = dmmoab->mbiface->get_entities_by_dimension(msets[i], dmmoab->dim, msetelems, true);MB_CHK_ERR(merr);
#ifdef MOAB_HAVE_MPI
      /* filter all the non-owned and shared entities out of the list */
      merr = dmmoab->pcomm->filter_pstatus(msetelems, PSTATUS_NOT_OWNED, PSTATUS_NOT); MBERRNM(merr);
#endif

      int partID;
      moab::EntityHandle mset=msets[i];
      merr = dmmoab->mbiface->tag_get_data(dmmoab->material_tag, &mset, 1, &partID);MB_CHK_ERR(merr);

      for (unsigned j=0; j < msetelems.size(); ++j)
        dmmoab->materials[dmmoab->elocal->index(msetelems[j])]=partID;
    }
  }

  PetscFunctionReturn(0);
}


/*@
  DMMoabCreateVertices - Creates and adds several vertices to the primary set represented by the DM.

  Collective

  Input Parameters:
+ dm - The DM object
. type - The type of element to create and add (Edge/Tri/Quad/Tet/Hex/Prism/Pyramid/Polygon/Polyhedra)
. conn - The connectivity of the element
- nverts - The number of vertices that form the element

  Output Parameter:
. overts  - The list of vertices that were created (can be NULL)

  Level: beginner

.seealso: DMMoabCreateSubmesh(), DMMoabCreateElement()
@*/
PetscErrorCode DMMoabCreateVertices(DM dm, const PetscReal* coords, PetscInt nverts, moab::Range* overts)
{
  moab::ErrorCode     merr;
  DM_Moab            *dmmoab;
  moab::Range         verts;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(coords, 2);

  dmmoab = (DM_Moab*) dm->data;

  /* Insert new points */
  merr = dmmoab->mbiface->create_vertices(&coords[0], nverts, verts); MBERRNM(merr);
  merr = dmmoab->mbiface->add_entities(dmmoab->fileset, verts); MBERRNM(merr);

  if (overts) *overts = verts;
  PetscFunctionReturn(0);
}


/*@
  DMMoabCreateElement - Adds an element of specified type to the primary set represented by the DM.

  Collective

  Input Parameters:
+ dm - The DM object
. type - The type of element to create and add (Edge/Tri/Quad/Tet/Hex/Prism/Pyramid/Polygon/Polyhedra)
. conn - The connectivity of the element
- nverts - The number of vertices that form the element

  Output Parameter:
. oelem  - The handle to the element created and added to the DM object

  Level: beginner

.seealso: DMMoabCreateSubmesh(), DMMoabCreateVertices()
@*/
PetscErrorCode DMMoabCreateElement(DM dm, const moab::EntityType type, const moab::EntityHandle* conn, PetscInt nverts, moab::EntityHandle* oelem)
{
  moab::ErrorCode     merr;
  DM_Moab            *dmmoab;
  moab::EntityHandle  elem;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidPointer(conn, 3);

  dmmoab = (DM_Moab*) dm->data;

  /* Insert new element */
  merr = dmmoab->mbiface->create_element(type, conn, nverts, elem); MBERRNM(merr);
  merr = dmmoab->mbiface->add_entities(dmmoab->fileset, &elem, 1); MBERRNM(merr);

  if (oelem) *oelem = elem;
  PetscFunctionReturn(0);
}


/*@
  DMMoabCreateSubmesh - Creates a sub-DM object with a set that contains all vertices/elements of the parent
  in addition to providing support for dynamic mesh modifications. This is useful for AMR calculations to
  create a DM object on a refined level.

  Collective

  Input Parameters:
. dm - The DM object

  Output Parameter:
. newdm  - The sub DM object with updated set information

  Level: advanced

.seealso: DMCreate(), DMMoabCreateVertices(), DMMoabCreateElement()
@*/
PetscErrorCode DMMoabCreateSubmesh(DM dm, DM *newdm)
{
  DM_Moab            *dmmoab;
  DM_Moab            *ndmmoab;
  moab::ErrorCode    merr;
  PetscErrorCode     ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);

  dmmoab = (DM_Moab*) dm->data;

  /* Create the basic DMMoab object and keep the default parameters created by DM impls */
  ierr = DMMoabCreateMoab(((PetscObject)dm)->comm, dmmoab->mbiface, &dmmoab->ltog_tag, PETSC_NULL, newdm);CHKERRQ(ierr);

  /* get all the necessary handles from the private DM object */
  ndmmoab = (DM_Moab*) (*newdm)->data;

  /* set the sub-mesh's parent DM reference */
  ndmmoab->parent = &dm;

  /* create a file set to associate all entities in current mesh */
  merr = ndmmoab->mbiface->create_meshset(moab::MESHSET_SET, ndmmoab->fileset); MBERR("Creating file set failed", merr);

  /* create a meshset and then add old fileset as child */
  merr = ndmmoab->mbiface->add_entities(ndmmoab->fileset, *dmmoab->vlocal); MBERR("Adding child vertices to parent failed", merr);
  merr = ndmmoab->mbiface->add_entities(ndmmoab->fileset, *dmmoab->elocal); MBERR("Adding child elements to parent failed", merr);

  /* preserve the field association between the parent and sub-mesh objects */
  ierr = DMMoabSetFieldNames(*newdm, dmmoab->numFields, dmmoab->fieldNames);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMMoabView_Ascii(DM dm, PetscViewer viewer)
{
  DM_Moab          *dmmoab = (DM_Moab*)(dm)->data;
  const char       *name;
  MPI_Comm          comm;
  PetscMPIInt       size;
  PetscErrorCode    ierr;

  PetscFunctionBegin;
  ierr = PetscObjectGetComm((PetscObject)dm, &comm);CHKERRQ(ierr);
  ierr = MPI_Comm_size(comm, &size);CHKERRQ(ierr);
  ierr = PetscObjectGetName((PetscObject) dm, &name);CHKERRQ(ierr);
  ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
  if (name) {ierr = PetscViewerASCIIPrintf(viewer, "%s in %D dimensions:\n", name, dmmoab->dim);CHKERRQ(ierr);}
  else      {ierr = PetscViewerASCIIPrintf(viewer, "Mesh in %D dimensions:\n", dmmoab->dim);CHKERRQ(ierr);}
  /* print details about the global mesh */
  {
    ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer, "Sizes: cells=%D, vertices=%D, blocks=%D\n", dmmoab->nele, dmmoab->n, dmmoab->bs);CHKERRQ(ierr);
    /* print boundary data */
    ierr = PetscViewerASCIIPrintf(viewer, "Boundary trace:\n", dmmoab->bndyelems->size(), dmmoab->bndyfaces->size(), dmmoab->bndyvtx->size());CHKERRQ(ierr);
    {
      ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      ierr = PetscViewerASCIIPrintf(viewer, "cells=%D, faces=%D, vertices=%D\n", dmmoab->bndyelems->size(), dmmoab->bndyfaces->size(), dmmoab->bndyvtx->size());CHKERRQ(ierr);
      ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    /* print field data */
    ierr = PetscViewerASCIIPrintf(viewer, "Fields: %D components\n", dmmoab->numFields);CHKERRQ(ierr);
    {
      ierr = PetscViewerASCIIPushTab(viewer);CHKERRQ(ierr);
      for (int i = 0; i < dmmoab->numFields; ++i) {
        ierr = PetscViewerASCIIPrintf(viewer, "[%D] - %s\n", i, dmmoab->fieldNames[i]);CHKERRQ(ierr);
      }
      ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
    }
    ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPopTab(viewer);CHKERRQ(ierr);
  ierr = PetscViewerFlush(viewer);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode DMMoabView_VTK(DM dm, PetscViewer v)
{
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode DMMoabView_HDF5(DM dm, PetscViewer v)
{
  PetscFunctionReturn(0);
}

PETSC_EXTERN PetscErrorCode DMView_Moab(DM dm, PetscViewer viewer)
{
  PetscBool      iascii, ishdf5, isvtk;
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  PetscValidHeaderSpecific(viewer, PETSC_VIEWER_CLASSID, 2);
  ierr = PetscObjectTypeCompare((PetscObject) viewer, PETSCVIEWERASCII, &iascii);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject) viewer, PETSCVIEWERVTK,   &isvtk);CHKERRQ(ierr);
  ierr = PetscObjectTypeCompare((PetscObject) viewer, PETSCVIEWERHDF5,  &ishdf5);CHKERRQ(ierr);
  if (iascii) {
    ierr = DMMoabView_Ascii(dm, viewer);CHKERRQ(ierr);
  } else if (ishdf5) {
#if defined(PETSC_HAVE_HDF5) && defined(MOAB_HAVE_HDF5)
    ierr = PetscViewerPushFormat(viewer, PETSC_VIEWER_HDF5_VIZ);CHKERRQ(ierr);
    ierr = DMMoabView_HDF5(dm, viewer);CHKERRQ(ierr);
    ierr = PetscViewerPopFormat(viewer);CHKERRQ(ierr);
#else
    SETERRQ(PetscObjectComm((PetscObject) dm), PETSC_ERR_SUP, "HDF5 not supported in this build.\nPlease reconfigure using --download-hdf5");
#endif
  }
  else if (isvtk) {
    ierr = DMMoabView_VTK(dm, viewer);CHKERRQ(ierr);
  }
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMInitialize_Moab(DM dm)
{
  PetscFunctionBegin;
  dm->ops->view                            = DMView_Moab;
  dm->ops->load                            = NULL /* DMLoad_Moab */;
  dm->ops->setfromoptions                  = DMSetFromOptions_Moab;
  dm->ops->clone                           = DMClone_Moab;
  dm->ops->setup                           = DMSetUp_Moab;
  dm->ops->createdefaultsection            = NULL;
  dm->ops->createdefaultconstraints        = NULL;
  dm->ops->createglobalvector              = DMCreateGlobalVector_Moab;
  dm->ops->createlocalvector               = DMCreateLocalVector_Moab;
  dm->ops->getlocaltoglobalmapping         = NULL;
  dm->ops->createfieldis                   = NULL;
  dm->ops->createcoordinatedm              = NULL /* DMCreateCoordinateDM_Moab */;
  dm->ops->getcoloring                     = NULL;
  dm->ops->creatematrix                    = DMCreateMatrix_Moab;
  dm->ops->createinterpolation             = DMCreateInterpolation_Moab;
  dm->ops->createinjection                 = NULL /* DMCreateInjection_Moab */;
  dm->ops->refine                          = DMRefine_Moab;
  dm->ops->coarsen                         = DMCoarsen_Moab;
  dm->ops->refinehierarchy                 = DMRefineHierarchy_Moab;
  dm->ops->coarsenhierarchy                = DMCoarsenHierarchy_Moab;
  dm->ops->globaltolocalbegin              = DMGlobalToLocalBegin_Moab;
  dm->ops->globaltolocalend                = DMGlobalToLocalEnd_Moab;
  dm->ops->localtoglobalbegin              = DMLocalToGlobalBegin_Moab;
  dm->ops->localtoglobalend                = DMLocalToGlobalEnd_Moab;
  dm->ops->destroy                         = DMDestroy_Moab;
  dm->ops->createsubdm                     = NULL /* DMCreateSubDM_Moab */;
  dm->ops->getdimpoints                    = NULL /* DMGetDimPoints_Moab */;
  dm->ops->locatepoints                    = NULL /* DMLocatePoints_Moab */;
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMClone_Moab(DM dm, DM *newdm)
{
  PetscErrorCode     ierr;

  PetscFunctionBegin;
  /* get all the necessary handles from the private DM object */
  (*newdm)->data = (DM_Moab*) dm->data;
  ((DM_Moab*)dm->data)->refct++;

  ierr = PetscObjectChangeTypeName((PetscObject) *newdm, DMMOAB);CHKERRQ(ierr);
  ierr = DMInitialize_Moab(*newdm);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}


PETSC_EXTERN PetscErrorCode DMCreate_Moab(DM dm)
{
  PetscErrorCode ierr;

  PetscFunctionBegin;
  PetscValidHeaderSpecific(dm, DM_CLASSID, 1);
  ierr = PetscNewLog(dm, (DM_Moab**)&dm->data);CHKERRQ(ierr);

  ((DM_Moab*)dm->data)->bs = 1;
  ((DM_Moab*)dm->data)->numFields = 1;
  ((DM_Moab*)dm->data)->n = 0;
  ((DM_Moab*)dm->data)->nloc = 0;
  ((DM_Moab*)dm->data)->nghost = 0;
  ((DM_Moab*)dm->data)->nele = 0;
  ((DM_Moab*)dm->data)->neleloc = 0;
  ((DM_Moab*)dm->data)->neleghost = 0;
  ((DM_Moab*)dm->data)->ltog_map = NULL;
  ((DM_Moab*)dm->data)->ltog_sendrecv = NULL;

  ((DM_Moab*)dm->data)->refct = 1;
  ((DM_Moab*)dm->data)->parent = NULL;
  ((DM_Moab*)dm->data)->vlocal = new moab::Range();
  ((DM_Moab*)dm->data)->vowned = new moab::Range();
  ((DM_Moab*)dm->data)->vghost = new moab::Range();
  ((DM_Moab*)dm->data)->elocal = new moab::Range();
  ((DM_Moab*)dm->data)->eghost = new moab::Range();

  ierr = DMInitialize_Moab(dm);CHKERRQ(ierr);
  PetscFunctionReturn(0);
}

