#pragma once
#ifndef DSPL_HPP
#define DSPL_HPP

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <list>
#include <numeric>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>

#include <omp.h>

#include "graph.hpp"
#include "utils.hpp"

/**
struct Comm {
  GraphElem size;
  GraphWeight degree;

  Comm() : size(0), degree(0.0) {};
};
*/

//GraphElem localCinfo_size;
//GraphWeight localCinfo_degree;


struct CommInfo {
    GraphElem community;
    GraphElem size;
    GraphWeight degree;
};

struct clmap_t {
  GraphElem f;
  GraphElem s;
};

#define CLMAP_MAX_NUM 32
#define COUNT_MAX_NUM 32

#if !defined(USE_OMP_OFFLOAD) && defined(__ARM_ARCH) && __ARM_ARCH >= 8
#ifndef CACHE_LINE_SIZE_BYTES
#define CACHE_LINE_SIZE_BYTES   256
#endif
/* The zfill distance must be large enough to be ahead of the L2 prefetcher */
static const int ZFILL_DISTANCE = 100;

/* x-byte cache lines */
static const int ELEMS_PER_CACHE_LINE = CACHE_LINE_SIZE_BYTES / sizeof(GraphWeight);

static const int ELEMS_PER_CACHE_LINE_GE = CACHE_LINE_SIZE_BYTES / sizeof(GraphElem);

/* Offset from a[j] to zfill */
static const int ZFILL_OFFSET = ZFILL_DISTANCE * ELEMS_PER_CACHE_LINE;

static const int ZFILL_OFFSET_GE = ZFILL_DISTANCE * ELEMS_PER_CACHE_LINE_GE;

static inline void zfill(GraphWeight * a) 
{ asm volatile("dc zva, %0": : "r"(a)); }

static inline void zfill_GE(GraphElem * b)
{ asm volatile("dc zva, %0": : "r"(b)); }


#endif


//#ifdef ZFILL_CACHE_LINES
/**
void sumVertexDegree(const Graph &g, std::vector<GraphWeight> &vDegree, std::vector<GraphElem> &localCinfo_size, std::vector<GraphWeight> &localCinfo_degree)
{
  const GraphElem nv = g.get_nv();

  GraphElem NV_blk_sz = nv / ELEMS_PER_CACHE_LINE;
#pragma omp parallel for default(none), shared(g, vDegree, localCinfo_degree, localCinfo_size), firstprivate(nv, NV_blk_sz) schedule(static)
  //default(none), shared(g, vDegree, localCinfo), firstprivate(nv, NV_blk_sz) schedule(static)
  for (GraphElem i=0; i < NV_blk_sz; i++) {
	  GraphElem NV_beg = i * ELEMS_PER_CACHE_LINE;
	  GraphElem NV_end = std::min(nv, ((i + 1) * ELEMS_PER_CACHE_LINE) );
	  
    GraphWeight * const zfill_limit = vDegree.data() + NV_end - ZFILL_OFFSET;
    GraphWeight * const vDeg = vDegree.data() + NV_beg;
	  
	  if (vDeg + ZFILL_OFFSET < zfill_limit)
		  zfill(vDeg + ZFILL_OFFSET);

		  for(GraphElem j=0; j < ELEMS_PER_CACHE_LINE; j++) {
               GraphElem e0, e1;
		           g.edge_range(NV_beg + j, e0, e1);

			  for (GraphElem e = e0; e < e1; e++) {
				  Edge const& edge = g.get_edge(e);
				  vDeg[j] += edge.weight_;
			  }
		  
		  //localCinfo[i].degree = vDegree[i];
		  //localCinfo[i].size = 1L;
        localCinfo_degree[NV_beg + j] = vDeg[j];
        localCinfo_size[NV_beg + j] = 1L;   
    }
  }
}
*/

/**
#else
*/
void sumVertexDegree(const Graph &g, std::vector<GraphWeight> &vDegree, std::vector<GraphElem> &localCinfo_size, std::vector<GraphWeight> &localCinfo_degree)
{
  const GraphElem nv = g.get_nv();

#ifdef OMP_SCHEDULE_RUNTIME
#pragma omp parallel for default(none), shared(g, vDegree, localCinfo), schedule(guided)
#else
#pragma omp parallel for default(none), shared(g, vDegree, localCinfo_degree, localCinfo_size), firstprivate(nv) schedule(static)
#endif

  for (GraphElem i = 0; i < nv; i++) {
    GraphElem e0, e1;
    GraphWeight tw = 0.0;

    g.edge_range(i, e0, e1);

    for (GraphElem k = e0; k < e1; k++) {
      const Edge &edge = g.get_edge(k);
      tw += edge.weight_;
    }

    vDegree[i] = tw;

    //localCinfo[i].degree = vDegree[i];
    //localCinfo[i].size = 1L;
    
    localCinfo_degree[i] = vDegree[i];
    localCinfo_size[i] = 1L;
    

  }

} // sumVertexDegree

//#endif

GraphWeight calcConstantForSecondTerm(const std::vector<GraphWeight> &vDegree)
{
  GraphWeight localWeight = 0.0;

  const size_t vsz = vDegree.size();

#ifdef OMP_SCHEDULE_RUNTIME
#pragma omp parallel for shared(vDegree), reduction(+: localWeight) schedule(runtime)
#else
#pragma omp parallel for shared(vDegree), firstprivate(vsz), reduction(+: localWeight) schedule(static)
#endif  
  for (GraphElem i = 0; i < vsz; i++)
    localWeight += vDegree[i]; // Local reduction

  return (1.0 / static_cast<GraphWeight>(localWeight));
} // calcConstantForSecondTerm

void initComm(std::vector<GraphElem> &pastComm, std::vector<GraphElem> &currComm)
{
  const size_t csz = currComm.size();

#ifdef DEBUG_PRINTF  
  assert(csz == pastComm.size());
#endif

#ifdef OMP_SCHEDULE_RUNTIME
#pragma omp parallel for shared(pastComm, currComm) schedule(runtime)
#else
#pragma omp parallel for shared(pastComm, currComm) schedule(static)
#endif
  for (GraphElem i = 0L; i < csz; i++) {
    pastComm[i] = i;
    currComm[i] = i;
  }
} // initComm

void initLouvain(const Graph &g, std::vector<GraphElem> &pastComm, 
        std::vector<GraphElem> &currComm, std::vector<GraphWeight> &vDegree, 
        std::vector<GraphWeight> &clusterWeight, std::vector<GraphElem> &localCinfo_size, 
        std::vector<GraphElem> &localCupdate_size, std::vector<GraphWeight> &localCinfo_degree, 
        std::vector<GraphWeight> &localCupdate_degree,
       //std::vector<Comm> &localCinfo, 
       //std::vector<Comm> &localCupdate, 
       GraphWeight &constantForSecondTerm)
{
  const GraphElem nv = g.get_nv();

  vDegree.resize(nv);
  pastComm.resize(nv);
  currComm.resize(nv);
  clusterWeight.resize(nv);
  //localCinfo.resize(nv);
  //localCupdate.resize(nv);
  localCinfo_size.resize(nv);
  localCinfo_degree.resize(nv);
  localCupdate_size.resize(nv);
  localCupdate_degree.resize(nv);
  
  //std::cout << "vDegree.size=======" << vDegree.size() << '\n'; 
  //std::cout << "localCinfo_degree.size=======" << localCinfo_degree.size() << '\n'; 
 
  //sumVertexDegree(g, vDegree, localCinfo);
  sumVertexDegree(g, vDegree, localCinfo_size, localCinfo_degree);
  constantForSecondTerm = calcConstantForSecondTerm(vDegree);

  initComm(pastComm, currComm);
} // initLouvain

GraphElem getMaxIndex(clmap_t *clmap, int &clmap_size, GraphWeight *counter, int &counter_size,
			  const GraphWeight selfLoop, GraphElem * localCinfo_size, GraphWeight * localCinfo_degree, const GraphWeight vDegree, const GraphElem currSize, const GraphWeight currDegree, const GraphElem currComm, const GraphWeight constant)
{
  clmap_t *storedAlready;
  GraphElem maxIndex = currComm;
  GraphWeight curGain = 0.0, maxGain = 0.0;
  GraphWeight eix = counter[0] - selfLoop;

  GraphWeight ax = currDegree - vDegree;
  GraphWeight eiy = 0.0, ay = 0.0;

  GraphElem maxSize = currSize; 
  GraphElem size = 0;

 // storedAlready = clmap;

  for(storedAlready = clmap; storedAlready != clmap + clmap_size; storedAlready++){
  //do {
      if (currComm != storedAlready->f) {
          //ay = localCinfo[storedAlready->f].degree;
          //size = localCinfo[storedAlready->f].size; 
          ay = localCinfo_degree[storedAlready->f];
          //printf("%lf ay ===========\n", ay);
          size = localCinfo_size[storedAlready->f];
          //printf("%ld size ============\n", size);

          if (storedAlready->s < counter_size)
            eiy = counter[storedAlready->s];
            //printf("%lf eiy ===========\n", eiy);
          curGain = 2.0 * (eiy - eix) - 2.0 * vDegree * (ay - ax) * constant;
          if ((curGain > maxGain) || ((curGain == maxGain) && (curGain != 0.0) && (storedAlready->f < maxIndex))) {
              maxGain = curGain;
              maxIndex = storedAlready->f;
              maxSize = size;
          }
        //printf("%lf eiy ===========\n", eiy);
      }//storedAlready++;

  } //while (storedAlready != clmap + clmap_size);

  if ((maxSize == 1) && (currSize == 1) && (maxIndex > currComm))
    maxIndex = currComm;
  //printf("%lf eiy ===========\n", eiy);
  return maxIndex;
} // getMaxIndex

GraphWeight buildLocalMapCounter(const GraphElem e0, const GraphElem e1, clmap_t *clmap, int &clmap_size, 
    GraphWeight *counter, int &counter_size, const Edge *edge_list, const GraphElem *currComm,
	  const GraphElem vertex)
{
  GraphElem numUniqueClusters = 1L;
  GraphWeight selfLoop = 0;
  clmap_t *storedAlready;
  for (GraphElem j = e0; j < e1; j++) {
        
    const Edge &edge = edge_list[j];
    const GraphElem &tail_ = edge.tail_;
    const GraphWeight &weight = edge.weight_;
    GraphElem tcomm;

    if (tail_ == vertex)
      selfLoop += weight;

    tcomm = currComm[tail_];

    storedAlready = clmap;
    for (int i = 0; i < clmap_size; i++, storedAlready++) {
      if (clmap[i].f == tcomm)
        break;
    }
    
    if (storedAlready != clmap + clmap_size && storedAlready->s < counter_size)
      counter[storedAlready->s] += weight;
    else {
        if (clmap_size < CLMAP_MAX_NUM) {
          clmap[clmap_size].f = tcomm;
          clmap[clmap_size].s = numUniqueClusters;
          clmap_size++;
        }
        if (counter_size < COUNT_MAX_NUM) {
          counter[counter_size] = weight;
          counter_size++;
        }
        numUniqueClusters++;
    }
  }

  return selfLoop;
} // buildLocalMapCounter

void execLouvainIteration(const GraphElem i, const GraphElem *edge_indices, const Edge *edge_list,
    const GraphElem *currComm, GraphElem *targetComm, const GraphWeight *vDegree, GraphElem * localCinfo_size, GraphWeight* localCinfo_degree, GraphElem * localCinfo_size_update, GraphWeight* localCinfo_degree_update, const GraphWeight constantForSecondTerm, GraphWeight *clusterWeight)
{
  GraphElem localTarget = -1;
  GraphElem e0, e1, selfLoop = 0;
  clmap_t clmap[CLMAP_MAX_NUM];
  int clmap_size = 0;
  GraphWeight counter[COUNT_MAX_NUM];
  int counter_size = 0;

  const GraphElem cc = currComm[i];
  GraphWeight ccDegree;
  GraphElem ccSize;  

  //ccDegree = localCinfo[cc].degree;
  //ccSize = localCinfo[cc].size;

  ccDegree = localCinfo_degree[cc];
  //printf("%d ccDegree\n", ccDegree);
  ccSize = localCinfo_size[cc];

  e0 = edge_indices[i];
  e1 = edge_indices[i+1];

  if (e0 != e1) {
    clmap[0].f = cc;
    clmap[0].s = 0;
    clmap_size++;
    counter[0] = 0.0;
    counter_size++;

    selfLoop =  buildLocalMapCounter(e0, e1, clmap, clmap_size, counter, 
        counter_size, edge_list, currComm, i);

    clusterWeight[i] += counter[0];

    localTarget = getMaxIndex(clmap, clmap_size, counter, counter_size, selfLoop, localCinfo_size, localCinfo_degree, vDegree[i], ccSize, ccDegree, cc, constantForSecondTerm);
  }
  else
    localTarget = cc;

  if ((localTarget != cc) && (localTarget != -1)) {
        
        //#pragma omp atomic update
        //localCupdate[localTarget].degree += vDegree[i];
        localCinfo_degree_update[localTarget] += vDegree[i];
        //#pragma omp atomic update
        //localCupdate[localTarget].size++;
        localCinfo_size_update[localTarget]++;
        //#pragma omp atomic update
        //localCupdate[cc].degree -= vDegree[i];
        localCinfo_degree_update[cc] -= vDegree[i];
        //#pragma omp atomic update
        //localCupdate[cc].size--;
        localCinfo_size_update[cc]--;
     }	



#ifdef DEBUG_PRINTF  
  assert(localTarget != -1);
#endif
  targetComm[i] = localTarget;
  
} // execLouvainIteration

GraphWeight computeModularity(const Graph &g, GraphWeight * localCinfo_degree,
			     const GraphWeight *clusterWeight,
			     const GraphWeight constantForSecondTerm)
{
  const GraphElem nv = g.get_nv();
  GraphWeight le_xx = 0.0, la2_x = 0.0;

	//map(to: clusterWeight[0:nv], localCinfo[0:nv]) 
#if defined(USE_OMP_OFFLOAD)
#pragma omp target teams distribute parallel for \
	reduction(+: le_xx), reduction(+: la2_x)
#elif defined(OMP_SCHEDULE_RUNTIME)
#pragma omp parallel for shared(clusterWeight, localCinfo), \
  reduction(+: le_xx), reduction(+: la2_x) schedule(runtime)
#else
#pragma omp parallel for shared(clusterWeight, localCinfo_degree), \
  reduction(+: le_xx), reduction(+: la2_x) schedule(static)
#endif
  for (GraphElem i = 0L; i < nv; i++) {
    le_xx += clusterWeight[i];
    //la2_x += localCinfo[i].degree * localCinfo[i].degree;
    la2_x += localCinfo_degree[i] * localCinfo_degree[i]; 
  } 

  GraphWeight currMod = (le_xx * constantForSecondTerm) - 
      (la2_x * constantForSecondTerm * constantForSecondTerm);
#ifdef DEBUG_PRINTF  
  std::cout << "[" << me << "]le_xx: " << le_xx << ", la2_x: " << la2_x << std::endl;
#endif

  return currMod;
} // computeModularity



//#ifdef ZFILL_CACHE_LINES
void updateLocalCinfo(const GraphElem nv, GraphElem * localCinfo_size, GraphWeight * localCinfo_degree, GraphElem * localCinfo_size_update, GraphWeight* localCinfo_degree_update)
{
#pragma omp for schedule(static)

    for (GraphElem i = 0L; i < nv; i++) {
        localCinfo_size[i] += localCinfo_size_update[i];
        localCinfo_degree[i] += localCinfo_degree_update[i];
    }
}
/**
//#else
void updateLocalCinfo(const GraphElem nv, Comm *localCinfo, const Comm *localCupdate)
{
#if defined(USE_OMP_OFFLOAD)
#pragma omp target teams distribute parallel for 
#elif defined(OMP_SCHEDULE_RUNTIME)
#pragma omp for schedule(runtime)
#else
#pragma omp for schedule(static)
#endif
    for (GraphElem i = 0L; i < nv; i++) {
        localCinfo[i].size += localCupdate[i].size;
        localCinfo[i].degree += localCupdate[i].degree;
    }
}
#endif
*/

//#ifdef ZFILL_CACHE_LINES

void cleanCWandCU(const GraphElem nv, GraphWeight *clusterWeight, GraphElem * localCinfo_size_update, GraphWeight* localCinfo_degree_updat)
{
#pragma omp for schedule(static)
    for (GraphElem i = 0L; i < nv; i++) {
        clusterWeight[i] = 0;
        localCinfo_degree_updat[i] = 0;
        localCinfo_size_update[i] = 0;
    }
} // distCleanCWandCU

/**
#else
void cleanCWandCU(const GraphElem nv, GraphWeight *clusterWeight,
        Comm *localCupdate)
{
#if defined(USE_OMP_OFFLOAD)
#pragma omp target teams distribute parallel for 
#elif defined(OMP_SCHEDULE_RUNTIME)
#pragma omp for schedule(runtime)
#else
#pragma omp for schedule(static)
#endif
    for (GraphElem i = 0L; i < nv; i++) {
        clusterWeight[i] = 0;
        localCupdate[i].degree = 0;
        localCupdate[i].size = 0;
    }
} // distCleanCWandCU

#endif
*/

GraphWeight louvainMethod(const Graph &g, const GraphWeight lower, const GraphWeight thresh, int &iters)
{
  // Times
  double time_start[6], time_end[6];
  std::vector<GraphElem> pastComm, currComm, targetComm;
  std::vector<GraphWeight> vDegree;
  std::vector<GraphWeight> clusterWeight;
  std::vector<GraphElem> localCinfo_size, localCupdate_size;
  std::vector<GraphWeight> localCinfo_degree, localCupdate_degree;
  
 
  const GraphElem nv = g.get_nv();

  GraphWeight constantForSecondTerm;
  GraphWeight prevMod = lower;
  GraphWeight currMod = -1.0;
  int numIters = 0;
  
  time_start[0] = omp_get_wtime(); 
  initLouvain(g, pastComm, currComm, vDegree, clusterWeight, localCinfo_size, localCupdate_size, 
          localCinfo_degree, localCupdate_degree, constantForSecondTerm);
  time_end[0] = omp_get_wtime() - time_start[0];
  targetComm.resize(nv);

#ifdef DEBUG_PRINTF  
  std::cout << "constantForSecondTerm: " << constantForSecondTerm << std::endl;
  std::cout << "Threshold: " << thresh << std::endl;
#endif

  const GraphElem *d_edge_indices = &g.edge_indices_[0];
  const Edge *d_edge_list = &g.edge_list_[0];
  GraphElem *d_currComm = &currComm[0];
  const GraphWeight *d_vDegree = &vDegree[0];
  GraphElem *d_targetComm = &targetComm[0];

//#ifdef ZFILL_CACHE_LINES  
  
  GraphElem *d_localCinfo_size = &localCinfo_size[0];
  GraphWeight *d_localCinfo_degree = &localCinfo_degree[0];
  GraphElem *d_localCinfo_size_update = &localCupdate_size[0];
  GraphWeight *d_localCinfo_degree_update = &localCupdate_degree[0];

//#else
//  Comm *d_localCinfo = &localCinfo[0];
//  Comm *d_localCupdate = &localCupdate[0];
//#endif
  

  GraphWeight *d_clusterWeight = &clusterWeight[0];

  double t_start = omp_get_wtime();
#if defined(USE_OMP_OFFLOAD)
#pragma omp target enter data map(to: d_clusterWeight[0:nv], d_localCupdate[0:nv])
#pragma omp target enter data map(to: d_edge_indices[0:nv+1], d_edge_list[0:ne], d_vDegree[0:nv], d_localCinfo[0:nv])
//#pragma omp target enter data map(to: d_targetComm[0:nv])
#endif

  // start Louvain iteration
  while(true) 
  {
#ifdef DEBUG_PRINTF  
    double t2 = omp_get_wtime();
    std::cout << "Starting Louvain iteration: " << numIters << std::endl;
#endif
    numIters++;
	time_start[1] = omp_get_wtime();
#if defined(USE_OMP_OFFLOAD)
#else
#pragma omp parallel default(shared) shared(clusterWeight, currComm, localCupdate_size, localCupdate_degree, targetComm, vDegree, pastComm, g), firstprivate(constantForSecondTerm)
  
//#pragma omp parallel default(shared) shared(clusterWeight, localCupdate, currComm, targetComm, \
//        vDegree, localCinfo, pastComm, g), \
//    firstprivate(constantForSecondTerm)

#endif
    {
        cleanCWandCU(nv, d_clusterWeight, d_localCinfo_size_update, d_localCinfo_degree_update);
      time_end[1] += (omp_get_wtime() - time_start[1]);
      time_start[2] = omp_get_wtime();
#if defined(USE_OMP_OFFLOAD)
#pragma omp target teams distribute parallel for \
	map(to: d_currComm [0:nv]) \
    	map(from: d_targetComm [0:nv])\
	thread_limit(TEAM_SIZE)
  	//tile size(64)
#elif defined(OMP_SCHEDULE_RUNTIME)
#pragma omp for schedule(runtime)
#else
#pragma omp for schedule(guided) 
//#pragma omp for schedule(static)
#endif
        for (GraphElem i = 0; i < nv; i++) {
            execLouvainIteration(i, d_edge_indices, d_edge_list, d_currComm, d_targetComm, d_vDegree, d_localCinfo_size, d_localCinfo_degree, d_localCinfo_size_update, d_localCinfo_degree_update, constantForSecondTerm, d_clusterWeight);

        }

      //printf("Ahhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhh");

      time_end[2] += (omp_get_wtime() - time_start[2]);
    }
     time_start[3] = omp_get_wtime();
#if defined(USE_OMP_OFFLOAD)
#else
//#pragma omp parallel shared(localCinfo, localCupdate)
#pragma omp parallel shared(localCinfo_size, localCupdate_size, localCinfo_degree, localCupdate_degree)
#endif
    {
        updateLocalCinfo(nv, d_localCinfo_size, d_localCinfo_degree, d_localCinfo_size_update, d_localCinfo_degree_update);
    }

      time_end[3] += (omp_get_wtime() - time_start[3]);
      time_start[4] = omp_get_wtime();
    // compute modularity
    currMod = computeModularity(g, d_localCinfo_degree, d_clusterWeight, constantForSecondTerm);
      time_end[4] += (omp_get_wtime() - time_start[4]);

    // exit criteria
    if (currMod - prevMod < thresh)
        break;

    prevMod = currMod;
    if (prevMod < lower)
        prevMod = lower;

    time_start[5] = omp_get_wtime();
#ifdef OMP_SCHEDULE_RUNTIME
#pragma omp parallel for \
    shared(pastComm, d_currComm, d_targetComm) \
    schedule(runtime)
#else
#pragma omp parallel for \
    shared(pastComm, d_currComm, d_targetComm) firstprivate(nv) \
    schedule(static)
#endif
    for (GraphElem i = 0; i < nv; i++) {
        GraphElem tmp = pastComm[i];
        pastComm[i] = d_currComm[i];
        d_currComm[i] = d_targetComm[i];
        d_targetComm[i] = tmp;
    }
    
      time_end[5] += (omp_get_wtime() - time_start[5]);

	
  } // end of Louvain iteration
  
  std::cout << "Louvain initLouvain time: " << time_end[0] << std::endl;
  std::cout << "Louvain cleanCWandCU time: " << time_end[1] << std::endl;
  std::cout << "Louvain execLouvainIteration time: " << time_end[2] << std::endl;
  std::cout << "Louvain updateLocalCinfo time: " << time_end[3] << std::endl;
  std::cout << "Louvain computeModularity time: " << time_end[4] << std::endl;
  std::cout << "Louvain update time (host): " << time_end[5] << std::endl;
  std::cout << "Louvain execution time: " << omp_get_wtime() - t_start << std::endl;


  iters = numIters;

  vDegree.clear();
  pastComm.clear();
  currComm.clear();
  targetComm.clear();
  clusterWeight.clear();
  //localCinfo.clear();
  localCinfo_size.clear();
  localCinfo_degree.clear();
  //localCupdate.clear();
  localCupdate_size.clear();
  localCupdate_degree.clear();
  
  return prevMod;
} // louvainMethod

#endif // __DSPL
