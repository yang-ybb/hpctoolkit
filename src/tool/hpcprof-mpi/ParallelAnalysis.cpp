// -*-Mode: C++;-*-

// * BeginRiceCopyright *****************************************************
//
// $HeadURL$
// $Id$
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
//
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
//
// Copyright ((c)) 2002-2018, Rice University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage.
//
// ******************************************************* EndRiceCopyright *

//***************************************************************************
//
// File:
//   $HeadURL$
//
// Purpose:
//   [The purpose of this file]
//
// Description:
//   [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

//**************************** MPI Include Files ****************************

#include <mpi.h>

//************************* System Include Files ****************************

#include <iostream>

#include <string>
using std::string;

#include <algorithm>

#include <stdint.h>

//*************************** User Include Files ****************************

#include <include/uint.h>

#include "ParallelAnalysis.hpp"

#include <lib/analysis/CallPath.hpp>
#include <lib/analysis/Util.hpp>

#include <lib/support/diagnostics.h>
#include <lib/support/StrUtil.hpp>


//*************************** Forward Declarations **************************

#define DBG_CCT_MERGE 0

//***************************************************************************


namespace ParallelAnalysis {

//***************************************************************************
// forward declarations
//***************************************************************************

static void
packStringSet(const StringSet& profile,
		 uint8_t** buffer, size_t* bufferSz);

static StringSet*
unpackStringSet(uint8_t* buffer, size_t bufferSz);

//***************************************************************************
// private functions
//***************************************************************************

static void 
broadcast_sizet
(
  size_t &size, 
  int root, 
  MPI_Comm comm
)
{ 
  long size_l = size;
  MPI_Bcast(&size_l, 1, MPI_LONG, root, comm);
  size = size_l;
} 



//***************************************************************************
// interface functions
//***************************************************************************

void
broadcast
(
  Prof::CallPath::Profile*& profile,
  int myRank, 
  int maxRank, 
  int rootRank, 
  MPI_Comm comm
)
{
  size_t size = 0;
  uint8_t* buf = NULL;

  if (myRank == rootRank) {
    packProfile(*profile, &buf, &size);
  }

  broadcast_sizet(size, rootRank, comm);

  if (myRank != rootRank) {
    buf = new uint8_t[size];
  }

  MPI_Bcast(buf, size, MPI_BYTE, rootRank, comm);

  if (myRank != 0) {
    profile = unpackProfile(buf, size);
  }

  delete [] buf;

  if (myRank == rootRank) {
    profile->metricMgr()->mergePerfEventStatistics_finalize(maxRank);
  }
}


void
broadcast
(
  StringSet &stringSet,
  int myRank, 
  int maxRank, 
  int rootRank, 
  MPI_Comm comm
)
{
  size_t size = 0;
  uint8_t* buf = NULL;

  if (myRank == rootRank) {
    packStringSet(stringSet, &buf, &size);
  }

  broadcast_sizet(size, rootRank, comm);

  if (myRank != rootRank) {
    buf = new uint8_t[size];
  }

  MPI_Bcast(buf, size, MPI_BYTE, rootRank, comm);

  if (myRank != 0) {
    StringSet *rhs = unpackStringSet(buf, size);
    stringSet += *rhs;
    delete rhs;
  }

  delete [] buf;
}


void
mergeNonLocal(Prof::CallPath::Profile* profile, int rank_x, int rank_y,
	      int myRank, MPI_Comm comm)
{
  int tag = rank_y; // sender

  uint8_t* profileBuf = NULL;

  Prof::CallPath::Profile* profile_x = NULL;
  Prof::CallPath::Profile* profile_y = NULL;

  if (myRank == rank_x) {
    profile_x = profile;

    // rank_x probes rank_y
    int profileBufSz = 0;
    MPI_Status mpistat;
    MPI_Probe(rank_y, tag, comm, &mpistat);
    MPI_Get_count(&mpistat, MPI_BYTE, &profileBufSz);
    profileBuf = new uint8_t[profileBufSz];

    // rank_x receives profile from rank_y
    MPI_Recv(profileBuf, profileBufSz, MPI_BYTE, rank_y, tag, comm, &mpistat);

    profile_y = unpackProfile(profileBuf, (size_t)profileBufSz);
    delete[] profileBuf;

    if (DBG_CCT_MERGE) {
      string pfx0 = "[" + StrUtil::toStr(rank_x) + "]";
      string pfx1 = "[" + StrUtil::toStr(rank_y) + "]";
      DIAG_DevMsgIf(1, profile_x->metricMgr()->toString(pfx0.c_str()));
      DIAG_DevMsgIf(1, profile_y->metricMgr()->toString(pfx1.c_str()));
    }
    
    int mergeTy = Prof::CallPath::Profile::Merge_MergeMetricByName;
    profile_x->merge(*profile_y, mergeTy);

    // merging the perf event statistics
    profile_x->metricMgr()->mergePerfEventStatistics(profile_y->metricMgr());

    if (DBG_CCT_MERGE) {
      string pfx = ("[" + StrUtil::toStr(rank_y)
		    + " => " + StrUtil::toStr(rank_x) + "]");
      DIAG_DevMsgIf(1, profile_x->metricMgr()->toString(pfx.c_str()));
    }

    delete profile_y;
  }

  if (myRank == rank_y) {
    profile_y = profile;

    size_t profileBufSz = 0;
    packProfile(*profile_y, &profileBuf, &profileBufSz);

    // rank_y sends profile to rank_x
    MPI_Send(profileBuf, (int)profileBufSz, MPI_BYTE, rank_x, tag, comm);

    free(profileBuf);
  }
}


void
mergeNonLocal(std::pair<Prof::CallPath::Profile*,
	                ParallelAnalysis::PackedMetrics*> data,
	      int rank_x, int rank_y, int myRank, MPI_Comm comm)
{
  int tag = rank_y; // sender

  if (myRank == rank_x) {
    Prof::CallPath::Profile* profile_x = data.first;
    ParallelAnalysis::PackedMetrics* packedMetrics_x = data.second;

    // rank_x receives metric data from rank_y
    MPI_Status mpistat;
    MPI_Recv(packedMetrics_x->data(), packedMetrics_x->dataSize(),
	     MPI_DOUBLE, rank_y, tag, comm, &mpistat);
    DIAG_Assert(packedMetrics_x->verify(), DIAG_UnexpectedInput);
    
    unpackMetrics(*profile_x, *packedMetrics_x);
  }

  if (myRank == rank_y) {
    Prof::CallPath::Profile* profile_y = data.first;
    ParallelAnalysis::PackedMetrics* packedMetrics_y = data.second;

    packMetrics(*profile_y, *packedMetrics_y);
    
    // rank_y sends metric data to rank_x
    MPI_Send(packedMetrics_y->data(), packedMetrics_y->dataSize(),
	     MPI_DOUBLE, rank_x, tag, comm);
  }
}


void
mergeNonLocal(StringSet *stringSet, int rank_x, int rank_y,
	      int myRank, MPI_Comm comm)
{
  int tag = rank_y; // sender

  uint8_t* stringSetBuf = NULL;

  StringSet *stringSet_x = NULL;
  StringSet *stringSet_y = NULL;

  if (myRank == rank_x) {
    stringSet_x  = stringSet;

    int stringSetBufSz = 0;

    // determine size of incoming packed directory set from rank_y
    MPI_Status mpistat;
    MPI_Probe(rank_y, tag, comm, &mpistat);
    MPI_Get_count(&mpistat, MPI_BYTE, &stringSetBufSz);

    stringSetBuf = new uint8_t[stringSetBufSz];

    // rank_x receives stringSet from rank_y
    MPI_Recv(stringSetBuf, stringSetBufSz, MPI_BYTE, 
	     rank_y, tag, comm, &mpistat);

    stringSet_y = unpackStringSet(stringSetBuf, 
				  (size_t) stringSetBufSz);
    delete[] stringSetBuf;

    *stringSet_x += *stringSet_y;

    delete stringSet_y;
  }

  if (myRank == rank_y) {
    stringSet_y = stringSet;

    size_t stringSetBufSz = 0;
    packStringSet(*stringSet_y, &stringSetBuf, &stringSetBufSz);

    // rank_y sends stringSet to rank_x
    MPI_Send(stringSetBuf, (int)stringSetBufSz, MPI_BYTE, 
	     rank_x, tag, comm);

    free(stringSetBuf);
  }
}


//***************************************************************************

void
packProfile(const Prof::CallPath::Profile& profile,
	    uint8_t** buffer, size_t* bufferSz)
{
  // open_memstream: mallocs buffer and sets bufferSz
  FILE* fs = open_memstream((char**)buffer, bufferSz);

  uint wFlags = Prof::CallPath::Profile::WFlg_VirtualMetrics;
  Prof::CallPath::Profile::fmt_fwrite(profile, fs, wFlags);

  fclose(fs);
}


Prof::CallPath::Profile*
unpackProfile(uint8_t* buffer, size_t bufferSz)
{
  FILE* fs = fmemopen(buffer, bufferSz, "r");

  Prof::CallPath::Profile* prof = NULL;
  uint rFlags = Prof::CallPath::Profile::RFlg_VirtualMetrics;
  Prof::CallPath::Profile::fmt_fread(prof, fs, rFlags,
				     "(ParallelAnalysis::unpackProfile)",
				     NULL, NULL);

  fclose(fs);
  return prof;
}



//***************************************************************************

static void
packStringSet(const StringSet& stringSet,
	      uint8_t** buffer, size_t* bufferSz)
{
  // open_memstream: malloc buffer and sets bufferSz
  FILE* fs = open_memstream((char**)buffer, bufferSz);

  StringSet::fmt_fwrite(stringSet, fs);

  fclose(fs);
}


static StringSet*
unpackStringSet(uint8_t* buffer, size_t bufferSz)
{
  FILE* fs = fmemopen(buffer, bufferSz, "r");

  StringSet* stringSet = NULL;

  StringSet::fmt_fread(stringSet, fs);

  fclose(fs);

  return stringSet;
}


//***************************************************************************

void
packMetrics(const Prof::CallPath::Profile& profile,
	    ParallelAnalysis::PackedMetrics& packedMetrics)
{
  Prof::CCT::Tree& cct = *profile.cct();

  // pack derived metrics [mDrvdBeg, mDrvdEnd) from 'profile' into
  // 'packedMetrics'
  uint mDrvdBeg = packedMetrics.mDrvdBegId();
  uint mDrvdEnd = packedMetrics.mDrvdEndId();

  DIAG_Assert(packedMetrics.numNodes() == cct.maxDenseId() + 1, "");
  DIAG_Assert(packedMetrics.numMetrics() == mDrvdEnd - mDrvdBeg, "");

  for (Prof::CCT::ANodeIterator it(cct.root()); it.Current(); ++it) {
    Prof::CCT::ANode* n = it.current();
    for (uint mId1 = 0, mId2 = mDrvdBeg; mId2 < mDrvdEnd; ++mId1, ++mId2) {
      packedMetrics.idx(n->id(), mId1) = n->metric(mId2);
    }
  }
}


void
unpackMetrics(Prof::CallPath::Profile& profile,
	      const ParallelAnalysis::PackedMetrics& packedMetrics)
{
  Prof::CCT::Tree& cct = *profile.cct();

  // 1. unpack 'packedMetrics' into temporary derived metrics [mBegId,
  //    mEndId) in 'profile'
  uint mBegId = packedMetrics.mBegId(), mEndId = packedMetrics.mEndId();

  DIAG_Assert(packedMetrics.numNodes() == cct.maxDenseId() + 1, "");
  DIAG_Assert(packedMetrics.numMetrics() == mEndId - mBegId, "");

  for (uint nodeId = 1; nodeId < packedMetrics.numNodes(); ++nodeId) {
    for (uint mId1 = 0, mId2 = mBegId; mId2 < mEndId; ++mId1, ++mId2) {
      Prof::CCT::ANode* n = cct.findNode(nodeId);
      n->demandMetric(mId2) = packedMetrics.idx(nodeId, mId1);
    }
  }

  // 2. update derived metrics [mDrvdBeg, mDrvdEnd) based on new
  //    values in [mBegId, mEndId)
  uint mDrvdBeg = packedMetrics.mDrvdBegId();
  uint mDrvdEnd = packedMetrics.mDrvdEndId();
  cct.root()->computeMetricsIncr(*profile.metricMgr(), mDrvdBeg, mDrvdEnd,
				 Prof::Metric::AExprIncr::FnCombine);
}



//***************************************************************************

} // namespace ParallelAnalysis
