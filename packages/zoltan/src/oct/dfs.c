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
static char *cvs_dfsc_id = "$Id$";
#endif

#include "lb_const.h"
#include "octant_const.h"
#include "dfs.h"
#include "costs_const.h"
#include "msg_const.h"
#include "migoct_const.h"
#include "util_const.h"

static int CLOSE = 0;           /* determines criterion for visiting octants */
static int DFS_Part_Count;      /* count of number of times LB_dfs_partition
                                   was called */

/*****************************************************************************/
/*
 * void LB_dfs_set_visit_criterion(int visit)
 *
 * sets criterion for visiting subtree. default is use dfs ordering
 */
void LB_dfs_set_visit_criterion(int visit) {
  if((visit < 0) || (visit > 1)) {
    fprintf(stderr, "%s\n%s\n", 
      "Warning: invalid visit criterion passed to LB_dfs_set_visit_criterion.",
      "Using default method of visiting octants. (CLOSE = 0)");
  }
  else {
    CLOSE = visit;
  }
}

/*****************************************************************************/
/*
 * void LB_dfs_partition()
 * 
 * This function calls the different subfunctions to partition the octree 
 */
void LB_dfs_partition(LB *lb, int *counter, float *c1) {
  float mycost,                     /* cost of the octant */
        globalcost,                 /* costs of all the octants */
        prefcost;                   /* sum of costs from previous processors */
  int nprevoct;                     /* the number of previous octants */
  int id;                           /* octant id */
  void *temp;                       /* temp variable for iterations */
  pRList localroots;                /* list of the local roots */
  pOctant lr,                       /* pointer to local root octant */
          oct;                      /* pointer to an octant */

  DFS_Part_Count = 0;
  *c1 = mycost = LB_costs_global_compute();
 
#ifdef LGG_MIGOCT
  /* gets the number of octants from the previous processors */
  nprevoct=LB_msg_int_scan(lb->Communicator, lb->Proc, POC_nOctants());

  /* add nprevoct to the id counter */
  localroots = POC_localroots();                  /* get the local root list */
  /* iterate through, and advance each id by nprevocts */
  /* this is trying to make all the octant id's to be unquie globally */
  while(localroots != NULL) {
    LB_dfs_SetIds(localroots->oct, nprevoct);
    localroots = localroots->next;
  }
#endif /* LGG_MIGOCT */

  /* Sum a value from each processor, and return sum to all processors */
  MPI_Allreduce(&mycost,&globalcost,1,MPI_FLOAT,MPI_SUM,lb->Communicator);
  prefcost=LB_msg_float_scan(lb->Communicator, lb->Proc, mycost);
  
  /* Initialize static vars */
  optcost=globalcost/lb->Num_Proc;               /* Optimal partition size */
  partition=prefcost/optcost;                /* Start work on this partition */
  if (partition==lb->Num_Proc)
    partition=lb->Num_Proc-1;

  total=partition*optcost;               /* Total cost of all previous parts */
  pcost=prefcost-partition*optcost;                /* Current partition cost */

  pmass=0.0;                            /* initialize octant volume variable */
  vector_set_comp(pcoord,0,0,0);

#if 0 
    LB_print_sync_start(lb, TRUE);
    printf("Global %6.1f  Pref %6.1f  Opt %6.1f  Pnum %2d  Pcost %8.2f\n",
	   globalcost,prefcost,optcost,partition,pcost);
    LB_print_sync_end(lb, TRUE);
#endif

  LB_visit_all_subtrees();

  (*counter) = DFS_Part_Count;
}


#ifdef LGG_MIGOCT
/*
 * int LB_dfs_SetIds(pOctant octant, int number_of_previous_octants)
 *
 * sets the ids of all the octants so that there is a global numbering
 */
int LB_dfs_SetIds(pOctant oct, int nprevoct) {
  int id,                                      /* the id number of an octant */
      i;                                       /* index counter */
  pOctant child;                               /* ith child of an octant */

  if(POC_isTerminal(oct)) {
    id = POC_id(oct);
    POC_setID(oct, id+nprevoct);                     /* now have global id's */
  }
  else {
    for(i=0; i<8; i++) {
      child = POC_child(oct, i);
      if (child != NULL)
	LB_dfs_SetIds(child, nprevoct);
    }
    id = POC_id(oct);
    POC_setID(oct, id+nprevoct);                     /* now have global id's */
  }
  return 0;
}
#endif /* LGG_MIGOCT */

/*****************************************************************************/
/*
 * void LB_visit_all_subtrees()
 *
 * visits each of the subtrees that are on the local processor
 */
void LB_visit_all_subtrees() {
  pRList localroots;                              /* list of all local roots */

  /* get the list of all the local roots */
  localroots = POC_localroots();

  /* iterate through each root in localroot list */
  while (localroots != NULL) {
    LB_visit(localroots->oct);   /* mark each subtree as being LB_visited */
#if 0
    /* clear attached cost data (is it needed?) */
    LB_costs_free(localroots->oct);
#endif
    localroots = localroots->next;
  }
}

/*****************************************************************************/
/*
 * void LB_visit(pOctant octant)
 * 
 * This routine references the following (static) global variables:
 *
 *   partition - (RW) number of the partition we are currently working on
 *   total     - (RW) total cost of all *previous* partitions
 *   pcost     - (RW) partition cost for current partition
 *   optcost   - (RO) optimal partition cost
 */
void LB_visit(pOctant octant) {
  float cost;                 /* Cost of this octant */
  float togo;                 /* Remaining room in current partition */
  float behind;               /* How many to make up for from all prev parts */
  pOctant children[8];        /* children of the octant */
  int i;                      /* index counter */
  COORD origin;               /* center of the octant */
  double volume;              /* volume of the octant */
  double prod[3];             /* product of octant origin and its volume */

  DFS_Part_Count++;
  cost = LB_costs_value(octant);               /* get the cost of the octant */
  behind = partition * optcost - total;          /* calcuate how much behind */

  /* If octant does not overflow the current partition, then use it. */
  if( cost==0 || (pcost+cost) <= (optcost+behind)) {
    LB_tag_subtree(octant,partition);
    pcost+=cost;
    
    POC_origin_volume(octant, origin, &volume);
    
    vector_cmult(prod,volume,origin);
    pmass+=volume;
    vector_add(pcoord,pcoord,prod);
    
    return;
  }

  /* 
   * Can't use entire octant because it is too big. If it has suboctants, 
   * visit them.
   */
  
  if (!POC_isTerminal(octant)) {
    POC_modify_newpid(octant, partition);                         /* Nonterm */
    POC_children(octant,children);

    /* currently CLOSE is defined to be 0, a functionality not used */
    if (CLOSE) {
      i=0;
      LB_visit_by_dist(octant,children);
    }
    else
      for (i=0; i<8; i++)                    /* Simple - just visit in order */
	/* if (children[i] && POC_local(children[i])) */
	if(children[i] && POC_local(octant,i))
	  LB_visit(children[i]);
    return;
  }
  
  /* 
   * No suboctants!
   * We've hit bottom - have to decide whether to add to
   * the current partition or start a new one.
   */
  togo = behind + optcost - pcost;

  if ((cost-togo) >= togo ) {
    /*
     * End current partition and start new one. We are more "over" than "under"
     */
    partition++;                                /* Move on to next partition */
    total += pcost;
    pcost = 0;
    pmass = 0;
    vector_set_comp(pcoord,0,0,0);
  }

  /*** Add terminal octant to current partition */
  POC_modify_newpid(octant, partition);
  pcost += cost;

  /* POC_size_and_origin(octant, size, origin); */
  POC_origin_volume(octant, origin, &volume);

  vector_cmult(prod,volume,origin);
  pmass += volume;
  vector_add(pcoord,pcoord,prod);
}

/*****************************************************************************/
/*
 * void LB_tag_subtree(pOctant octant, int partition_number)
 *
 * marks all the octants within the subtree to be in the current partition
 */
void LB_tag_subtree(pOctant octant, int partition) {
  pOctant children[8];                                 /* children of octant */
  int i;                                               /* index counter */

  /* modify NPID so octant know where to migrate to */
  POC_modify_newpid(octant, partition);

  if (POC_isTerminal(octant))
    return;

  /* if octant has children, have to tag them too */
  POC_children(octant,children);
  
  for (i=0; i<8; i++)                        /* Simple - just visit in order */
    /* if (children[i] && POC_local(children[i])) */
    if(children[i] && POC_local(octant,i))
      LB_tag_subtree(children[i],partition);
}

/*****************************************************************************/
/*
 * void LB_dfs_migrate(LB_TAG **export_tags, int *number_of_sent_tags,
 *                     LB_TAG **import_tags, int *number_of_received_tags)
 *
 * sets up information so the migrate octant routines can create the
 * proper export_tags and import_tags arrays
 */
void LB_dfs_migrate(LB *lb, pRegion *export_regs, int *nsentags,
		    pRegion *import_regs, int *nrectags, 
		    float *c2, float *c3, int *counter3, int *counter4) 
{
  pRList lroots;                              /* list of all local roots */
  pOctant root;                               /* root of a subtree */
  pOctant oct;                                /* octree octant */
  pOctant *docts = NULL;                      /* array of octants being sent */
  int *dpids = NULL;                          /* array of octant pids */
  int dcount;                                 /* count of octants being sent */
  int pid;                                    /* processor id */
  int i;                                      /* index counter */

  i = POC_nOctants();
  if(POC_nOctants()) {          /* allocate space for octants being migrated */
    docts = (pOctant *)malloc(sizeof(pOctant)*POC_nOctants());
    dpids = (int *)malloc(sizeof(int)*POC_nOctants());
    if((docts == NULL) || (dpids == NULL)) {
      fprintf(stderr, "Dfs_Migrate: cannot allocate arrays.\n");
      abort();
    }
  }

  dcount=0;

  /* go through the local octants and make sure each has a valid npid */
  lroots = POC_localroots();
  while(lroots != NULL) { 
    for (oct=lroots->oct; oct; oct=POC_nextDfs(oct)) {
      pid = POC_data_newpid(oct);
      if (pid<0 || pid>=lb->Num_Proc) {
	fprintf(stderr,"%d LB_dfs_migrate: bad dest pid %d\n", lb->Proc, pid);
	abort();
      }
      docts[dcount]=oct;
      dpids[dcount++]=pid;
    }
    lroots = lroots->next;
  }

  if (dcount!=POC_nOctants()) {
    fprintf(stderr, "ERROR: in LB_dfs_migrate, octant count mismatch\n");
    abort();
  }

  /* setup the import_regs and export_regs */
  LB_Migrate_Objects(lb, docts, dpids, dcount, export_regs, nsentags, 
		     import_regs, nrectags, c2, c3, counter3, counter4);

  free(docts);
  free(dpids);
  
#if 0
  LB_print_sync_start(lb, TRUE);
    printf("LB_dfs_migrate: sending %d regions\n",nregions);
  LB_print_sync_end(lb, TRUE);
  {
    pMesh mesh;
    int local_meshregions, global_meshregions;
    int global_migrate;

    mesh=pmdb_get_pmesh(pmeshpb);
    local_meshregions=M_nRegion(mesh);
    global_meshregions=msg_int_sum(local_meshregions);

    global_migrate=msg_int_sum(nregions);
    
    if(lb->Proc == 0) { 
      printf("OCTPART volume: %8d of %8d = %.3f\n",
	     global_migrate,global_meshregions,
	     (double)global_migrate/global_meshregions);
    }
  }
  
  LB_migreg_migrate_regions(lb, pmeshpb,migregions,nregions,TRUE);
  free(migregions);
#endif
}

/*****************************************************************************/
/*
 * void LB_visit_by_dist()
 *
 * tries to find the closest child to add to the partition 
 */
void LB_visit_by_dist(pOctant octant, pOctant children[8])
{
  COORD min,                    /* min bounds of the octant */
        max;                    /* max bounds of the octant */
  COORD cmin[8],                /* array of min bounds for octant's children */
        cmax[8];                /* array of max bounds for octant's children */
  COORD origin;                 /* the origin of the octant */
  COORD corigin[8];             /* array of origin pnts of octant's children */
  COORD pcentroid;              /* centroid of the octant */
  int i;                        /* index counter */
  int minchild;                 /* lowest numbered child */
  double dist,                  /* distance */
         mindist;               /* lowest distance */
  int visited[8];               /* flag showing which child has been visited */

  /* initializing data */
  mindist=0;
  pcentroid[0] = pcentroid[1] = pcentroid[2] = 0;

  /* get the bounds of the octant */
  POC_bounds(octant,min,max);

  /* use bounds to find octant's origin */
  LB_bounds_to_origin(min,max,origin);

  /* get bounds, and origin for each of the children */
  for (i=0; i<8; i++) {
    visited[i]=0;
    LB_child_bounds(min,max,origin,i,cmin[i],cmax[i]);
    LB_bounds_to_origin(cmin[i],cmax[i],corigin[i]);
  }
  
 
  for(minchild=0; minchild>=0; ) {        /* Visit child closest to centroid */
    minchild= -1;
    
    if (pmass>0)
      vector_divc(pcentroid,pcoord,pmass);
    
    /* for each of the child, find the one with the closest distance */
    for (i=0; i<8; i++)
      /* if ((POC_local(children[i])) && (!visited[i])) { */
      if(POC_local(octant, i) && !visited[i]) {
	dist=vector_dist(pcentroid,corigin[i]);
	if (pmass==0 || minchild<0 || dist<mindist) {
	  mindist=dist;
	  minchild=i;
	}
      }

    if (minchild>=0) {
      /* visit that child, so that it can be pu into the partition */
      LB_visit(children[minchild]);
      visited[minchild]=1;          /* mark the child as having been visited */
    }
  }
}
