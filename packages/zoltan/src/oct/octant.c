/*====================================================================
 * ------------------------
 * | CVS File Information |
 * ------------------------
 *
 * $RCSfile$
 *
 * $Author$
 *
 * $Date$
 *
 * $Revision$
 *
 *====================================================================*/
#ifndef lint
static char *cvs_octantc_id = "$Id$";
#endif

#include "lb_const.h"
#include "octant_const.h"
#include "util_const.h"

/*****************************************************************************/
/* WARNING: GLOBAL VARIABLES... BE CAREFUL WHEN USING */
pRList OCT_rootlist = NULL;          /* list of all the local roots          */
int OCT_localpid;                    /* the processor id                     */
COORD OCT_gmin;                      /* global root's min bounds             */
COORD OCT_gmax;                      /* global root's max bounds             */
int OCT_dimension;
int GRAY;
int HILBERT;

static int OCT_count;                /* count of all local octants           */
static int OCT_idcount;              /* count for id's, help with uniqueness */

/*****************************************************************************/
/*
 * void POC_init(int processor_id)
 *
 * sets up global variables for the octree partitioner
 */
void POC_init(int pid, int dim) {
  OCT_count=0;
  OCT_localpid=pid;
  OCT_rootlist=NULL;
  OCT_idcount=0;
  if((dim > 3) || (dim < 2)) {
    fprintf(stderr,"WARNING: illegal dimension, using default (3D).\n");
    OCT_dimension = 3;
  }
  else
    OCT_dimension = dim;
}

/*****************************************************************************/
/*
 * pOctant POC_malloc()
 *
 * creates space in memory for a new octant
 */
pOctant POC_malloc() {
  pOctant ptr;                                  /* pointer to the new octant */
  
  ptr=(pOctant)malloc(sizeof(Octant));                     /* allocate space */
  if (!ptr) {                                                 /* error check */
    perror("POC_malloc: malloc failure");
    abort();
  }

  OCT_count++;                                      /* increase octant count */
  return(ptr);
}

/*****************************************************************************/
/*
 * POC_new()
 *
 * create a new octant on the local processor and return
 * a pointer to it.  It will have no parents or children.
 */
pOctant POC_new() {
  pOctant new;                                  /* pointer to the new octant */
  int i;                                        /* index counter */
  
  new=POC_malloc();                       /* create space for the new octant */
  /* Null out child pointers, and invalidate cpids */
  for (i=0; i<8; i++) {
    new->child[i] = NULL;
    new->cpid[i] = -1;
  }
  /* setup default information about octant */
  new->parent= NULL;
  new->ppid = OCT_localpid;
  new->id=(OCT_idcount++);
  new->which = -1;
  new->numChild = 0;
  new->list=NULL;
  new->cost = 0;
  new->npid = OCT_localpid;
  /* new->orientation = -1; */
  return(new);
}

/*****************************************************************************/
/* 
 * void POC_free(pOctant octant)
 *
 * frees up space in memory
 * this does not delete the attached regions, must specifically call
 * POC_clearRegions 
 */
void POC_free(pOctant oct) {
  pRList ptr, prev;                      /* traversal pointers for root list */
  pRegion c;                             /* pointer to region */
  
  /* traverse through local root list, if octant a local root */
  ptr = OCT_rootlist;
  prev = NULL;
  if (oct->ppid != OCT_localpid) {
    while(ptr != NULL) {
      if(ptr->oct->id == oct->id) {
	if(ptr == OCT_rootlist)
	  OCT_rootlist = ptr->next;
	else
	  prev->next = ptr->next;
	ptr->oct = NULL;
	free(ptr);
	ptr = NULL;
      }
      else {
	prev = ptr;
	ptr = ptr->next;
      }
    }
  }
/********  
  c = oct->list;
  while(c != NULL) {
    oct->list = c->next;
    free(c);
    c = oct->list;
  }
********/

  /* free up space in memory */
  oct->list = NULL;
  OCT_count--;
  free(oct);
}

/*****************************************************************************/
#ifdef LGG_MIGOCT
/*
 * ATTN!! :
 * Manually setting the octant's id number is necessary if octants are 
 * being migrated this is not the case with the current implementation 
 * of the code references are left here in case it does become necessary
 * to do so again.
 */
/*
 * void POC_setID(pOctant octant, int id)
 *
 * sets the id of the octant
 */
void POC_setID(pOctant oct, int id) 
{ oct->id=id; }
#endif /* LGG_MIGOCT */

/*****************************************************************************/
/*
 * void POC_id(pOctant octant)
 *
 * gets the id of the octant 
 */
int POC_id(pOctant oct) 
{ return(oct->id); }

/*****************************************************************************/
/*
 * POC_setparent(pOctant octant, pOctant parent, int parent_processor_id)
 *
 * sets the parent of the octant. If the parent is offprocessor, then
 * add octant to the local root list
 */
void POC_setparent(pOctant oct, pOctant parent, int ppid) {
  pRList tmp,                          /* temp variable used for iterating */
         prev;                         /* pointer to previous root looked at */

  if (oct->ppid == OCT_localpid) {                              /* was local */
    if (ppid != OCT_localpid) {                              /* now nonlocal */
      tmp = (pRList)malloc(sizeof(RList));
      if(tmp == NULL) {
	fprintf(stderr, "%s: %s\n", "POC_setparent",
		"Could not allocate new rootlist entry.");
	abort();
      }
      tmp->oct = oct;
      prev = OCT_rootlist;
      if(prev != NULL) {
	/* add new root to end of rootlist */
	while(prev->next != NULL)
	  prev = prev->next;
	tmp->next = NULL;
	prev->next = tmp;
      }
      else {
	tmp->next = OCT_rootlist;
	OCT_rootlist = tmp;
      }
    }
  }
  else                                                        /* was foreign */
    if (ppid == OCT_localpid) {                               /* now local   */
      tmp = OCT_rootlist; 
      /* delete octant from local root list */
      while(tmp != NULL) {
	if(tmp->oct->id == oct->id) {
	  if(tmp == OCT_rootlist)
	    OCT_rootlist = tmp->next;
	  else
	    prev->next = tmp->next;
	  tmp->oct = NULL;
	  free(tmp);
	  tmp = NULL;
	}
	else {
	  prev = tmp;
	  tmp = tmp->next;
	}
      }
      /* PList_remItem(OCT_rootlist,oct); */
    }
  oct->ppid=ppid;
  oct->parent=parent;
}

/*****************************************************************************/
/*
 * void POC_setchildnum(pOctant octant, int childnumber)
 *
 * sets the child number of the octant
 */
void POC_setchildnum(pOctant oct, int childnum)
{ oct->which=childnum; }

/*****************************************************************************/
/* 
 * int POC_childnum(pOctant octant)
 * 
 * returns the child number of the octant
 */
int POC_childnum(pOctant oct)
{ return(oct->which); }

/*****************************************************************************/
/*
 * void POC_setcild(pOctant octant, int childnumber, pOctant child)
 *
 * sets the ith child pointer to point to the child
 */
void POC_setchild(pOctant oct, int i, pOctant child) {
  oct->child[i]=child;         /* need to make sure child's info is correct */
  oct->numChild++;
}

/*****************************************************************************/
/*
 * void POC_setchildren(pOctant octant, pOctant children[8],
 *                      int child_processor_ids[8])
 *
 * sets each child pointer of the octant to each of the children
 * in the array children[8]
 */
void POC_setchildren(pOctant oct, pOctant children[8], int cpids[8]) {
  int i;                                                    /* index counter */
  
  for (i=0; i<8; i++)
    POC_setchild(oct,i,children[i]);
}

/*****************************************************************************/
/*
 * void POC_setbounds(pOctant octant, COORD minimum_bounds,
 *                    COORD maximum_bounds)
 *
 * sets the min and max bounds of an octant 
 */
void POC_setbounds(pOctant oct, COORD min, COORD max)
{ vector_set(oct->min,min); vector_set(oct->max,max); }

/*****************************************************************************/
/*
 * void POC_bounds(pOctant octant, COORD minimum, COORD maximum)
 *
 * gets the min and max bounds of an octant
 */
void POC_bounds(pOctant oct, COORD min, COORD max)
{ vector_set(min,oct->min); vector_set(max,oct->max); }

/*****************************************************************************/
/*
 * pOctant POC_parent(pOctant octant)
 *
 * returns a pointer to the parent of the octant
 */
pOctant POC_parent(pOctant oct) 
{ return(oct->parent); }

/*****************************************************************************/
/*
 * pOctant POC_child(pOctant octant, int child_index)
 *
 * return the ith child of oct
 */
pOctant POC_child(pOctant oct, int i) 
{ return(oct->child[i]); }

/*****************************************************************************/
/*
 * POC_children(pOctant octant, pOctant children[8])
 *
 * fill in values of all an octants children
 */
int POC_children(pOctant oct, pOctant children[8]) {
  int i;                                                    /* index counter */
  
  for (i=0; i<8; i++)
    children[i] = POC_child(oct,i);
  return(oct->numChild);
}
  
/*****************************************************************************/
/*
 * int POC_isTerminal(pOctant octant)
 *
 * returns TRUE if the octant is terminal (has NO children)
 */
int POC_isTerminal(pOctant oct) {
  int i,                                         /* index counter */
      child;                                     /* flag identifying a child */

  child=0;
  for (i=0; (!child) && (i<8) ; i++)
    child=(int)oct->child[i];
  if(child == 0)
    return(1);
  else
    return(0);
}

/*****************************************************************************/
/*
 * pRegion POC_regionlist(pOctant octant)
 * get a copy of the octant's region list
 */
pRegion POC_regionlist(pOctant oct) {
  if (!POC_isTerminal(oct))
    abort();
  return(oct->list);
}

/*****************************************************************************/
/*
 * void POC_addRegion(pOctant octant, pRegion region)
 * add a region to oct's list
 */
void POC_addRegion(pOctant oct, pRegion region) { 
  pRegion entry;                      /* pointer to new entry in region list */

  if(oct == NULL) {
    fprintf(stderr,"%s\n",
	    "ERROR in POC_addRegion, trying to insert into null octant.");
    abort();
  }

  entry = (pRegion)malloc(sizeof(Region));        /* malloc space for region */
  if(entry == NULL) {
    fprintf(stderr,"%s\n", 
	    "ERROR in POC_addRegion, cannot allocate memory for region");
    abort();
  }
  /* copy region information into the entry */
  vector_set(entry->Coord, region->Coord);
  entry->Weight = region->Weight;
  LB_SET_GID(entry->Tag.Global_ID, region->Tag.Global_ID);
  LB_SET_LID(entry->Tag.Local_ID, region->Tag.Local_ID);
  entry->Tag.Proc = region->Tag.Proc;

  /* attach region to region list */
  entry->next = oct->list; 
  oct->list = entry;
}

/*****************************************************************************/
/*
 * void POC_remRegion(pOctant octant, pRegion region)
 * remove a region from an oct's list
 */
void POC_remRegion(pOctant oct, pRegion region) { 
  pRegion tmp,                       /* temp var used for iterating */
          prev;                      /* pointer to previous region looked at */
  
  tmp = oct->list;
  /* iterate through region list to find region to be removed */
  while(tmp != NULL) {
    if (LB_EQ_GID(tmp->Tag.Global_ID, region->Tag.Global_ID)) {
      if(tmp == oct->list)
	oct->list = tmp->next;
      else
	prev->next = tmp->next;
      free(tmp);
      tmp = NULL;
    }
    else {
      prev = tmp;
      tmp = tmp->next;
    }
  }
}

/*****************************************************************************/
/*
 * void POC_clearRegions(pOctant octant)
 * erase all of a oct's regions
 */
void POC_clearRegions(pOctant oct) { 
  pRegion ptr;                                         /* pointer to regions */
  
  ptr = oct->list;
  while(ptr != NULL) {
    oct->list = ptr->next;
    free(ptr);
    ptr = oct->list;
  }
  oct->list=NULL;
}

/*****************************************************************************/
/*
 * int POC_nRegions(pOctant octant)
 * return the number of regions in the octant's list
 */
int POC_nRegions(pOctant oct) {
  pRegion ptr;                     /* pointer to iterate through region list */
  int count;                       /* count of number of regions in list */

  if (oct == NULL) {
    fprintf(stderr, "ERROR in POC_nRegions, tried to access NULL data\n");
    abort();
  }
  if(!POC_isTerminal(oct)) {
    fprintf(stderr, "ERROR in POC_nRegions, tried to access non-Terminal\n");
    abort();
  }
  count = 0;
  ptr = oct->list;
  while(ptr != NULL) {
    count ++;
    ptr = ptr->next;
  }
  return(count);
}

/*****************************************************************************/
/*
 * pRList POC_localroots()
 *
 * return the list of local roots
 */
pRList POC_localroots()
{
  return(OCT_rootlist);
}

/*****************************************************************************/
/***                        attached data routines                         ***/
/*
 * void POC_modify_cost(pOctant octant, float cost)
 *
 * modifies the cost field of the octant
 */
void POC_modify_cost(pOctant oct, float cost)
{
  oct->cost = cost;
}

/*****************************************************************************/
/*
 * void POC_modify_newpid(pOctant octant, int new_processor_id)
 *
 * modifies the npid field of the octant, telling where the octant should
 * migrate to
 */
void POC_modify_newpid(pOctant oct, int newpid)
{
  oct->npid = newpid;
}

/*****************************************************************************/
/*
 * float POC_data_cost(pOctant octant)
 *
 * returns the cost of the octant
 */
float POC_data_cost(pOctant oct)
{
  return(oct->cost);
}

/*****************************************************************************/
/*
 * int POC_data_newpid(pOctant octant)
 *
 * returns the new processor id of the octant 
 */
int POC_data_newpid(pOctant oct)
{ 
  return(oct->npid);
}

/*****************************************************************************/
/*
 * int POC_nlocal(pOctant octant)
 *
 * return the number of local leaves in the subtree
 */
int POC_nlocal(pOctant oct) {
  int i;                                    /* index counter */
  pOctant child;                            /* child of an octant */
  int total;                                /* total number of local octants */

  if (POC_isTerminal(oct))
    return(1);
  total=0;
  for (i=0; i<8; i++) {
    child = POC_child(oct,i);
    if (child)
      total+=POC_nlocal(child);
  }
  return(total);
}

/*****************************************************************************/
/*
 * int POC_nOctant()
 *
 * returns the number of octants on local processor
 */
int POC_nOctants()
{ return (OCT_count); }

/*****************************************************************************/
/*
 * void POC_origin_volume(pOctant oct, COORD origin, double *volume)
 *
 * gets the origin and volume of the octant
 */
void POC_origin_volume(pOctant oct, COORD origin, double *volume) {
  COORD min,                                         /* octant minimum bound */
        max;                                         /* octant maximum bound */
  double size[3];                                    /* size of the octant */

  POC_bounds(oct,min,max);
  LB_bounds_to_origin_size(min,max,origin,size);  
  *volume=size[0]*size[1]*size[2];
}

/*****************************************************************************/
/*
 * void POC_printResults()
 *
 * prints out the intermediate results of the octree structure
 */
void POC_printResults() {
  pRList ptr;                                  /* pointer to local root list */

  ptr = OCT_rootlist;
  /* go through each entry in local root list and travers down subtree */
  while(ptr != NULL) {
    POC_DfsTraversal(ptr->oct);
    ptr = ptr->next;
  }
}

/*****************************************************************************/
/*
 * void POC_DfsTraversal(pOctant octant)
 *
 * traverse through the octree in DFS order to get a printout
 */
void POC_DfsTraversal(pOctant oct) {
  int i;                                                    /* index counter */

  if(oct == NULL)
    return;
  if(POC_isTerminal(oct))
    POC_printRegionInfo(oct);
  else {
    for(i=0; i<8; i++)
      POC_DfsTraversal(oct->child[i]);
    POC_printRegionInfo(oct);
  }
}

/*****************************************************************************/
/*
 * void POC_printRegionInfo(pOctant octant)
 *
 * prints out region information
 */
void POC_printRegionInfo(pOctant oct) {
  pRegion ptr;            /* pointer to iterate through octant's region list */
  pOctant parent;

#if 0
  if(!POC_isTerminal(oct)) {
    fprintf(stderr, "WARNING: printing regions in a non-terminal octant.\n");
    return;
  }
#endif

  parent = POC_parent(oct);
  printf("(Proc %d) ocant %d:\n",
	 OCT_localpid, oct->id);
  printf("\tbounds\tmin=%lf, %lf, %lf\n\t\t max %lf, %lf, %lf\n",
	 oct->min[0], oct->min[1], oct->min[2],
	 oct->max[0], oct->max[1], oct->max[2]);
  if(parent != NULL)
    printf("\tparent octant: %d", parent->id);
  else
    printf("\tparent octant: NULL");
  printf(" \tmigrate: from %d to %d\n", OCT_localpid, oct->npid);

  if(!POC_isTerminal(oct)) {
    return;
  }
  ptr = oct->list;

  if(ptr==NULL)
    printf("\tOctant is EMPTY\n");

  while(ptr != NULL) {
    printf("\tGlobal_ID:%ld Local_ID:%ld Proc:%ld coord:(%lf, %lf, %lf)\n", 
	   ptr->Tag.Global_ID, ptr->Tag.Local_ID, ptr->Tag.Proc,
	   ptr->Coord[0], ptr->Coord[1], ptr->Coord[2]);
    /*
      printf("%lf %lf %lf,  %d -> %d\n", 
      ptr->Coord[0], ptr->Coord[1], ptr->Coord[2],
      OCT_localpid,oct->npid);
    */
    ptr = ptr->next;
  }
}

/*****************************************************************************/
/*
 * pOctant POC_nextDfs(pOctant octant)
 *
 * returns the next octant in a DFS ordering
 */
pOctant POC_nextDfs(pOctant octant) {
  pOctant parent,                                     /* parent of an octant */
          child;                                      /* child of an octant */
  int ppid,                                           /* parent processor id */
      childnum,                                       /* child number */
      cpid;                                           /* child processor id */
  int i;                                              /* index counter */

  if (!octant)
    return(NULL);

  for (i=0; i<8; i++) {
    child = POC_child(octant,i);
    if (child)
      return(child);          /* Go down */
  }

  parent = POC_parent(octant);
  while (parent) {
    for (i=octant->which; i<7; i++) {
      child = POC_child(parent,i+1);
      if (child)
	return(child);
    }

    octant=parent;                                       /* Go up */
    parent = POC_parent(octant);
  }
  
  return(NULL);         /* no more octants remain in dfs ordering */
}

/*****************************************************************************/
/*
 * int POC_local(pOctant octant, int child_index)
 *
 * returns true if ith child is local
 */
int POC_local(pOctant octant, int i) {
  if(octant->cpid[i] == OCT_localpid)
    return 1;
  else {
    /*
     *if(octant->cpid[i] == -1)
     * fprintf(stderr, "WARNING: cpid was not set, returning 0 as default.\n");
     */
    return 0;
  }
}

/*****************************************************************************/
/*
 * void POC_setCpid(pOctant octant, int child_index, int child_processor_id)
 *
 * sets the child processor id field of an octant
 */
void POC_setCpid(pOctant octant, int i, int cpid) {
  octant->cpid[i] = cpid;
}

/*****************************************************************************/
/*
 * int POC_delTree(pOctant root)
 *
 * recursivly traverses down root's subtree deleting all the octants
 */
int POC_delTree(pOctant root) {
  int i;                                               /* index counter */
  pOctant child;                                       /* child of an octant */
  
  if(root == NULL)
    return 1;

  if(POC_isTerminal(root)) {
    if(POC_nRegions(root))
      POC_clearRegions(root);
    POC_free(root);
  }
  else {
    for(i=0; i<8; i++) {
      child = POC_child(root, i);
      if(child != NULL)
	POC_delTree(child);
    }
    POC_free(root);
  }
  return 1;
}
