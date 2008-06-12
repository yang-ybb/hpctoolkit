// -*-Mode: C++;-*-
// $Id$

// * BeginRiceCopyright *****************************************************
// 
// Copyright ((c)) 2002-2007, Rice University 
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
//   $Source$
//
// Purpose:
//   [The purpose of this file]
//
// Description:
//   [The set of functions, macros, etc. defined in the file]
//
//***************************************************************************

//************************* System Include Files ****************************

#include <iostream>
using std::cout;
using std::cerr;
using std::endl;

#include <fstream>
#include <sstream>

#ifdef NO_STD_CHEADERS
# include <string.h>
#else
# include <cstring>
using namespace std; // For compatibility with non-std C headers
#endif

#include <string>
using std::string;

#include <map>
#include <list>
#include <vector>

#include <algorithm>

//************************ OpenAnalysis Include Files ***********************

#include <OpenAnalysis/CFG/ManagerCFG.hpp>
#include <OpenAnalysis/Utils/RIFG.hpp>
#include <OpenAnalysis/Utils/NestedSCR.hpp>
#include <OpenAnalysis/Utils/Exception.hpp>

//*************************** User Include Files ****************************

#include "bloop.hpp"
#include "bloop_LocationMgr.hpp"
#include "OAInterface.hpp"

#include <lib/prof-juicy/Struct-Tree.hpp>
using namespace Prof;

#include <lib/binutils/LM.hpp>
#include <lib/binutils/Seg.hpp>
#include <lib/binutils/Proc.hpp>
#include <lib/binutils/BinUtils.hpp>

#include <lib/support/diagnostics.h>
#include <lib/support/pathfind.h>
#include <lib/support/Logic.hpp>

//*************************** Forward Declarations ***************************

namespace banal {

namespace bloop {

// ------------------------------------------------------------
// Helpers for building a structure tree
// ------------------------------------------------------------

typedef std::multimap<Struct::Proc*, binutils::Proc*> ProcStrctToProcMap;


static ProcStrctToProcMap*
BuildLMSkeleton(Struct::LM* lmStrct, binutils::LM* lm);

static Struct::File*
FindOrCreateFileNode(Struct::LM* lmStrct, binutils::Proc* p);

static Struct::Proc*
FindOrCreateProcNode(Struct::File* fStrct, binutils::Proc* p);


static Struct::Proc*
BuildProcStructure(Struct::Proc* pStrct, binutils::Proc* p, 
		   bool irrIvalIsLoop, bool fwdSubstOff); 

static int
BuildProcLoopNests(Struct::Proc* pStrct, binutils::Proc* p,
		   bool irrIvalIsLoop, bool fwdSubstOff);

static int
BuildStmts(bloop::LocationMgr& locMgr,
	   Struct::ACodeNode* enclosingStrct, binutils::Proc* p,
	   OA::OA_ptr<OA::CFG::NodeInterface> bb);


static void
FindLoopBegLineInfo(binutils::Proc* p, 
		    OA::OA_ptr<OA::CFG::NodeInterface> headBB,
		    string& begFilenm, string& begProcnm, SrcFile::ln& begLn);

#if 0
static Struct::Stmt*
FindOrCreateStmtNode(std::map<SrcFile::ln, Struct::Stmt*>& stmtMap,
		     Struct::ACodeNode* enclosingStrct, SrcFile::ln line, 
		     VMAInterval& vmaint);
#endif

// Cannot make this a local class because it is used as a template
// argument! Sigh.
class QNode {
public:
  QNode(OA::RIFG::NodeId x = OA::RIFG::NIL, 
	Struct::ACodeNode* y = NULL, Struct::ACodeNode* z = NULL)
    : fgNode(x), enclosingStrct(y), scope(z) { }
  
  bool isProcessed() const { return (scope != NULL); }
  
  OA::RIFG::NodeId fgNode;  // flow graph node
  Struct::ACodeNode* enclosingStrct; // enclosing scope of 'fgNode'
  Struct::ACodeNode* scope;          // scope for children of 'fgNode' (fgNode scope)
};

  
} // namespace bloop

} // namespace banal


//*************************** Forward Declarations ***************************

namespace banal {

namespace bloop {

// ------------------------------------------------------------
// Helpers for normalizing the structure tree
// ------------------------------------------------------------

static bool 
RemoveOrphanedProcedureRepository(Prof::Struct::Tree* strctTree);

static bool 
MergeBogusAlienStrct(Prof::Struct::Tree* strctTree);

static bool
CoalesceDuplicateStmts(Prof::Struct::Tree* strctTree, 
		       bool unsafeNormalizations);

static bool 
MergePerfectlyNestedLoops(Prof::Struct::Tree* strctTree);

static bool 
RemoveEmptyNodes(Prof::Struct::Tree* strctTree);

static bool 
FilterFilesFromStrctTree(Prof::Struct::Tree* strctTree, 
			 const char* canonicalPathList);

// ------------------------------------------------------------
// Helpers for normalizing the structure tree
// ------------------------------------------------------------
class StmtData;

class SortIdToStmtMap : public std::map<int, StmtData*> {
public:
  typedef std::map<int, StmtData*> My_t;

public:
  SortIdToStmtMap() { }
  virtual ~SortIdToStmtMap() { clear(); }
  virtual void clear();
};

class StmtData {
public:
  StmtData(Struct::Stmt* _stmt = NULL, int _level = 0)
    : stmt(_stmt), level(_level) { }
  ~StmtData() { /* owns no data */ }

  Struct::Stmt* GetStmt() const { return stmt; }
  int GetLevel() const { return level; }
  
  void SetStmt(Struct::Stmt* _stmt) { stmt = _stmt; }
  void SetLevel(int _level) { level = _level; }
  
private:
  Struct::Stmt* stmt;
  int level;
};

static void 
DeleteContents(Struct::ANodeSet* s);


// ------------------------------------------------------------
// Helpers for normalizing the structure tree
// ------------------------------------------------------------

class CDS_RestartException {
public:
  CDS_RestartException(Struct::ACodeNode* n) : node(n) { }
  ~CDS_RestartException() { }
  Struct::ACodeNode* GetNode() const { return node; }

private:
  Struct::ACodeNode* node;
};

} // namespace bloop

} // namespace banal

//*************************** Forward Declarations ***************************

#define DBG_PROC 0 /* debug BuildProcStructure */
#define DBG_CDS  0 /* debug CoalesceDuplicateStmts */

static string OrphanedProcedureFile = Prof::Struct::Tree::UnknownFileNm;
static string InferredProcedure     = Prof::Struct::Tree::UnknownProcNm;

static const char *PGMdtd =
#include <lib/xml/PGM.dtd.h>

//****************************************************************************
// Set of routines to write a structure tree
//****************************************************************************

// FIXME (minor): move to prof-juicy library for experiment writer
void
banal::bloop::WriteStrctTree(std::ostream& os, Struct::Tree* strctTree, 
			     bool prettyPrint)
{
  os << "<?xml version=\"1.0\"?>" << std::endl;
  os << "<!DOCTYPE PGM [\n" << PGMdtd << "]>" << std::endl;
  os.flush();

  int dumpFlags = (Struct::Tree::XML_TRUE); // Struct::ANode::XML_NO_ESC_CHARS
  if (!prettyPrint) { dumpFlags |= Struct::Tree::COMPRESSED_OUTPUT; }
  
  strctTree->xml_dump(os, dumpFlags);
}

//****************************************************************************
// Set of routines to build a scope tree
//****************************************************************************

// BuildLMStructure: Builds a scope tree -- with a focus on loop nest
//   recovery -- representing program source code from the load module
//   'lm'.
// A load module represents binary code, which is essentially
//   a collection of procedures along with some extra information like
//   symbol and debugging tables.  This routine relies on the debugging
//   information to associate procedures with thier source code files
//   and to recover procedure loop nests, correlating them to source
//   code line numbers.  A normalization pass is typically run in order
//   to 'clean up' the resulting scope tree.  Among other things, the
//   normalizer employs heuristics to reverse certain compiler
//   optimizations such as loop unrolling.
Prof::Struct::Tree*
banal::bloop::BuildLMStructure(binutils::LM* lm, 
			       const char* canonicalPathList, 
			       bool normalizeScopeTree,
			       bool unsafeNormalizations,
			       bool irreducibleIntervalIsLoop,
			       bool forwardSubstitutionOff)
{
  DIAG_Assert(lm, DIAG_UnexpectedInput);

  Struct::Tree* strctTree = NULL;
  Struct::Pgm* pgmStrct = NULL;

  // FIXME (minor): relocate
  OrphanedProcedureFile = "~~~" + lm->name() + ":<unknown-file>~~~";

  // Assume lm->Read() has been performed
  pgmStrct = new Struct::Pgm("");
  strctTree = new Struct::Tree("", pgmStrct);

  Struct::LM* lmStrct = new Struct::LM(lm->name(), pgmStrct);

  // 1. Build Struct::File/Struct::Proc skeletal structure
  ProcStrctToProcMap* mp = BuildLMSkeleton(lmStrct, lm);
  

  // 2. For each [Struct::Proc, binutils::Proc] pair, complete the build.
  // Note that a Struct::Proc may be associated with more than one
  // binutils::Proc.
  for (ProcStrctToProcMap::iterator it = mp->begin(); it != mp->end(); ++it) {
    Struct::Proc* pStrct = it->first;
    binutils::Proc* p = it->second;

    DIAG_Msg(2, "Building scope tree for [" << p->name()  << "] ... ");
    BuildProcStructure(pStrct, p, 
		       irreducibleIntervalIsLoop, forwardSubstitutionOff);
  }
  delete mp;

  // 3. Normalize
  if (canonicalPathList && canonicalPathList[0] != '\0') {
    bool result = FilterFilesFromStrctTree(strctTree, canonicalPathList);
    DIAG_Assert(result, "Path canonicalization result should never be false!");
  }

  if (normalizeScopeTree) {
    bool result = Normalize(strctTree, unsafeNormalizations);
    DIAG_Assert(result, "Normalization result should never be false!");
  }

  return strctTree;
}


// Normalize: Because of compiler optimizations and other things, it
// is almost always desirable normalize a scope tree.  For example,
// almost all unnormalized scope tree contain duplicate statement
// instances.  See each normalizing routine for more information.
bool 
banal::bloop::Normalize(Prof::Struct::Tree* strctTree,
			bool unsafeNormalizations)
{
  bool changed = false;
  
  // changed |= RemoveOrphanedProcedureRepository(strctTree);

  // Remove unnecessary alien scopes
  changed |= MergeBogusAlienStrct(strctTree);

  // Cleanup procedure/alien scopes
  changed |= CoalesceDuplicateStmts(strctTree, unsafeNormalizations);
  changed |= MergePerfectlyNestedLoops(strctTree);
  changed |= RemoveEmptyNodes(strctTree);
  
  return true; // no errors
}


//****************************************************************************
// Helpers for building a scope tree
//****************************************************************************

namespace banal {

namespace bloop {

// BuildLMSkeleton: Build skeletal file-procedure structure.  This
// will be useful later when detecting alien lines.  Also, the
// nesting structure allows inference of accurate boundaries on
// procedure end lines.
//
// A Struct::Proc can be associated with multiple binutils::Procs
//
// Struct::Procs will be sorted by begLn (cf. Struct::ACodeNode::Reorder)
static ProcStrctToProcMap*
BuildLMSkeleton(Struct::LM* lmStrct, binutils::LM* lm)
{
  ProcStrctToProcMap* mp = new ProcStrctToProcMap;
  
  // -------------------------------------------------------
  // 1. Create basic structure for each procedure
  // -------------------------------------------------------

  for (binutils::LM::ProcMap::iterator it = lm->procs().begin();
       it != lm->procs().end(); ++it) {
    binutils::Proc* p = it->second;
    if (p->size() != 0) {
      Struct::File* fStrct = FindOrCreateFileNode(lmStrct, p);
      Struct::Proc* pStrct = FindOrCreateProcNode(fStrct, p);
      mp->insert(make_pair(pStrct, p));
    }
  }

  // -------------------------------------------------------
  // 2. Establish nesting information:
  // -------------------------------------------------------
  for (ProcStrctToProcMap::iterator it = mp->begin(); it != mp->end(); ++it) {
    Struct::Proc* pStrct = it->first;
    binutils::Proc* p = it->second;
    binutils::Proc* parent = p->parent();

    if (parent) {
      Struct::Proc* parentScope = lmStrct->findProc(parent->begVMA());
      pStrct->Unlink();
      pStrct->Link(parentScope);
    }
  }

  // FIXME (minor): The following order is appropriate when we have
  // symbolic information. I.e. we, 1) establish nesting information
  // and then attempt to refine procedure bounds using nesting
  // information.  If such nesting information is not available,
  // assume correct bounds and attempt to establish nesting.
  
  // 3. Establish procedure bounds: nesting + first line ... [FIXME]

  // 4. Establish procedure groups: [FIXME: more stuff from DWARF]
  //      template instantiations, class member functions
  return mp;
}


// FindOrCreateFileNode: 
static Struct::File* 
FindOrCreateFileNode(Struct::LM* lmStrct, binutils::Proc* p)
{
  // Attempt to find filename for procedure
  string filenm = p->filename();
  p->lm()->realpath(filenm);
  
  if (filenm.empty()) {
    string procnm;
    SrcFile::ln line;
    p->GetSourceFileInfo(p->begVMA(), 0, procnm, filenm, line);
  }
  if (filenm.empty()) { 
    filenm = OrphanedProcedureFile; 
  }
  
  // Obtain corresponding Struct::File
  return Struct::File::findOrCreate(lmStrct, filenm);
} 


// FindOrCreateProcNode: Build skeletal Struct::Proc.  We can assume that
// the parent is always a Struct::File.
static Struct::Proc*
FindOrCreateProcNode(Struct::File* fStrct, binutils::Proc* p)
{
  // Find VMA boundaries: [beg, end)
  VMA endVMA = p->begVMA() + p->size();
  VMAInterval bounds(p->begVMA(), endVMA);
  DIAG_Assert(!bounds.empty(), "Attempting to add empty procedure " 
	      << bounds.toString());
  
  // Find procedure name
  string procNm   = GetBestFuncName(p->name()); 
  string procLnNm = GetBestFuncName(p->GetLinkName());
  
  // Find preliminary source line bounds
  string file, proc;
  SrcFile::ln begLn1, endLn1;
  binutils::Insn* eInsn = p->lastInsn();
  ushort endOp = (eInsn) ? eInsn->opIndex() : 0;
  p->GetSourceFileInfo(p->begVMA(), 0, p->endVMA(), endOp, 
		       proc, file, begLn1, endLn1, 0 /*no swapping*/);
  
  // Compute source line bounds to uphold invariant:
  //   (begLn == 0) <==> (endLn == 0)
  SrcFile::ln begLn, endLn;
  if (p->hasSymbolic()) {
    begLn = p->begLine();
    endLn = std::max(begLn, endLn1);
  }
  else {
    // for now, always assume begLn to be more accurate
    begLn = begLn1;
    endLn = std::max(begLn1, endLn1);
  }
  
  // Create or find the scope.  Fuse procedures if names match.
  Struct::Proc* pStrct = fStrct->FindProc(procNm, procLnNm);
  if (!pStrct) {
    pStrct = new Struct::Proc(procNm, fStrct, procLnNm, p->hasSymbolic(),
			   begLn, endLn);
  }
  else {
    // Assume the procedure was split.  Fuse it together.
    DIAG_DevMsg(3, "Merging multiple instances of procedure [" 
		<< pStrct->toStringXML() << "] with " << procNm << " " 
		<< procLnNm << " " << bounds.toString());
    pStrct->ExpandLineRange(begLn, endLn);
  }
  pStrct->vmaSet().insert(bounds);
  
  return pStrct;
}

} // namespace bloop

} // namespace banal


//****************************************************************************
// 
//****************************************************************************

#if (DBG_PROC)
static bool DBG_PROC_print_now = false;
#endif

namespace banal {

namespace bloop {


static int 
BuildProcLoopNests(Struct::Proc* enclosingProc, binutils::Proc* p,
		   OA::OA_ptr<OA::NestedSCR> tarj,
		   OA::OA_ptr<OA::CFG::CFGInterface> cfg, 
		   OA::RIFG::NodeId fgRoot, 
		   bool irrIvalIsLoop, bool fwdSubstOff);

static Struct::ACodeNode*
BuildLoopAndStmts(bloop::LocationMgr& locMgr, 
		  Struct::ACodeNode* enclosingStrct, binutils::Proc* p,
		  OA::OA_ptr<OA::NestedSCR> tarj,
		  OA::OA_ptr<OA::CFG::CFGInterface> cfg, 
		  OA::RIFG::NodeId fgNode, bool irrIvalIsLoop);


// BuildProcStructure: Complete the representation for 'pStrct' given the
// binutils::Proc 'p'.  Note that pStrcts parent may itself be a Struct::Proc.
static Struct::Proc* 
BuildProcStructure(Struct::Proc* pStrct, binutils::Proc* p,
		   bool irrIvalIsLoop, bool fwdSubstOff)
{
  DIAG_Msg(3, "==> Proc `" << p->name() << "' (" << p->id() << ") <==");
  
#if (DBG_PROC)
  DBG_PROC_print_now = false;
  uint dbgId = p->GetId();
  const char* dbgNm = "xxx";
  if (p->GetName().find(dbgNm) != string::npos) {
    DBG_PROC_print_now = true;
  }
  if (dbgId == 10) {
    DBG_PROC_print_now = true; 
  }
#endif
  
  BuildProcLoopNests(pStrct, p, irrIvalIsLoop, fwdSubstOff);
  
  return pStrct;
}


// BuildProcLoopNests: Build procedure structure by traversing
// the Nested SCR (Tarjan tree) to create loop nests and statement
// scopes.
static int
BuildProcLoopNests(Struct::Proc* pStrct, binutils::Proc* p,
		   bool irrIvalIsLoop, bool fwdSubstOff)
{
  try {
    using banal::OAInterface;
    
    OA::OA_ptr<OAInterface> irIF; irIF = new OAInterface(p);
    
    OA::OA_ptr<OA::CFG::ManagerCFGStandard> cfgmanstd;
    cfgmanstd = new OA::CFG::ManagerCFGStandard(irIF);
    OA::OA_ptr<OA::CFG::CFG> cfg = 
      cfgmanstd->performAnalysis(TY_TO_IRHNDL(p, OA::ProcHandle));
    
    OA::OA_ptr<OA::RIFG> rifg; 
    rifg = new OA::RIFG(cfg, cfg->getEntry(), cfg->getExit());
    OA::OA_ptr<OA::NestedSCR> tarj; tarj = new OA::NestedSCR(rifg);
    
    OA::RIFG::NodeId fgRoot = rifg->getSource();

#if (DBG_PROC)
    if (DBG_PROC_print_now) {
      cout << "*** CFG for `" << p->GetName() << "' ***" << endl;
      cout << "  total blocks: " << cfg->getNumNodes() << endl
	   << "  total edges:  " << cfg->getNumEdges() << endl;

      OA::OA_ptr<OA::OutputBuilder> ob1, ob2;
      ob1 = new OA::OutputBuilderText();
      ob2 = new OA::OutputBuilderDOT();

      cfg->configOutput(ob1);
      cfg->output(*irIF);

      cfg->configOutput(ob2);
      cfg->output(*irIF);

      cout << "*** Nested SCR (Tarjan Interval) Tree for `" << 
	p->GetName() << "' ***" << endl;
      tarj->dump(cout);
      cout << endl;
      cout.flush(); cerr.flush();
    }
#endif

    int r = BuildProcLoopNests(pStrct, p, tarj, cfg, fgRoot,
			       irrIvalIsLoop, fwdSubstOff);
    return r;
  }
  catch (const OA::Exception& x) {
    std::ostringstream os;
    x.report(os);
    DIAG_Throw("[OpenAnalysis] " << os.str());
  }
}


// BuildProcLoopNests: Visit the object code loops in pre-order and
// create source code representations.  Technically we choose to visit
// in BFS order, which in a better world would would allow us to check
// sibling loop boundaries.  Note that the resulting source coded
// loops are UNNORMALIZED.
static int 
BuildProcLoopNests(Struct::Proc* enclosingProc, binutils::Proc* p,
		   OA::OA_ptr<OA::NestedSCR> tarj,
		   OA::OA_ptr<OA::CFG::CFGInterface> cfg, 
		   OA::RIFG::NodeId fgRoot, 
		   bool irrIvalIsLoop, bool fwdSubstOff)
{
  typedef std::list<QNode> MyQueue;

  int nLoops = 0;

  std::vector<bool> isNodeProcessed(tarj->getRIFG()->getHighWaterMarkNodeId() + 1);
  
  bloop::LocationMgr locMgr(enclosingProc->AncLM());
#if DBG_PROC
  if (DBG_PROC_print_now) {
    locMgr.debug(1);
  }
#endif
  locMgr.begSeq(enclosingProc, fwdSubstOff);
  
  // -------------------------------------------------------
  // Process the Nested SCR (Tarjan tree) in preorder
  // -------------------------------------------------------

  // NOTE: The reason for this generality is that we experimented with
  // a "split" BFS search in a futile attempt to recover the inlining
  // tree.

  // Queue INVARIANTs.  The queue has two sections to support general searches:
  //
  //             BFS sec.      TODO sec.
  //   queue: [ e_i ... e_j | e_k ... e_l ]
  //            ^             ^
  //            begin()       q_todo
  // 
  //  1.  Nodes in BFS section have already been processed.
  //  1a. Processed nodes have non-NULL child-scopes
  //  2.  Nodes in TODO section have *not* been processed.
  //  2a. Non-processed nodes have NULL child-scopes
  //  3.  No node's children has been processed
  MyQueue theQueue;
  theQueue.push_back(QNode(fgRoot, enclosingProc, NULL));
  MyQueue::iterator q_todo = theQueue.begin();

  while (!theQueue.empty()) {
    // -------------------------------------------------------
    // 1. pop the front element, adjusting q_todo if necessary
    // -------------------------------------------------------
    bool isTODO = (q_todo == theQueue.begin());
    if (isTODO) {
      q_todo++;
    }
    QNode qnode = theQueue.front();
    theQueue.pop_front();

    // -------------------------------------------------------
    // 2. process the element, if necessary
    // -------------------------------------------------------
    if (isTODO) {
      // Note that if this call closes an alien context, invariants
      // below ensure that this context has been fully explored.
      Struct::ACodeNode* myScope;
      myScope = BuildLoopAndStmts(locMgr, qnode.enclosingStrct, p,
				  tarj, cfg, qnode.fgNode, irrIvalIsLoop);
      isNodeProcessed[qnode.fgNode] = true;
      qnode.scope = myScope;
      if (myScope->Type() == Struct::ANode::TyLOOP) {
	nLoops++;
      }
    }
    
    // -------------------------------------------------------
    // 3. process children within this context in BFS fashion
    //    (Note: WasCtxtClosed() always false -> BFS)
    // -------------------------------------------------------
    OA::RIFG::NodeId kid = tarj->getInners(qnode.fgNode);
    if (kid == OA::RIFG::NIL) {
      continue;
    }
    
    do {
      Struct::ACodeNode* myScope;
      myScope = BuildLoopAndStmts(locMgr, qnode.scope, p, 
				  tarj, cfg, kid, irrIvalIsLoop);
      isNodeProcessed[kid] = true;
      if (myScope->Type() == Struct::ANode::TyLOOP) {
	nLoops++;
      }
      
      // Insert to BFS section (inserts before)
      theQueue.insert(q_todo, QNode(kid, qnode.scope, myScope));
      kid = tarj->getNext(kid);
    }
    while (kid != OA::RIFG::NIL /* && !WasCtxtClosed(qnode.scope, locMgr) */);

    // NOTE: *If* we knew the loop was correctly parented in a
    // procedure context, we could check sibling boundaries.  However,
    // there are cases where an initial loop nesting must be
    // corrected.

    // -------------------------------------------------------
    // 4. place rest of children in queue's TODO section
    // -------------------------------------------------------
    for ( ; kid != OA::RIFG::NIL; kid = tarj->getNext(kid)) {
      theQueue.push_back(QNode(kid, qnode.scope, NULL));

      // ensure 'q_todo' points to the beginning of TODO section
      if (q_todo == theQueue.end()) {
	q_todo--;
      }
    }
  }

  // -------------------------------------------------------
  // Process nodes that we have not visited.
  //
  // This may occur if the CFG is disconnected.  E.g., we do not
  // correctly handle jump tables.  Note that we cannot be sure of the
  // location of these statements within procedure structure.
  // -------------------------------------------------------

  for (uint i = 1; i < isNodeProcessed.size(); ++i) {
    if (!isNodeProcessed[i]) {
      // INVARIANT: return value is never a Struct::Loop
      BuildLoopAndStmts(locMgr, enclosingProc, p, tarj, cfg, i, irrIvalIsLoop);
    }
  }

  
  locMgr.endSeq();

  return nLoops;
}


// BuildLoopAndStmts: Returns the expected (or 'original') enclosing
// scope for children of 'fgNode', e.g. loop or proc. 
static Struct::ACodeNode*
BuildLoopAndStmts(bloop::LocationMgr& locMgr, 
		  Struct::ACodeNode* enclosingStrct, binutils::Proc* p,
		  OA::OA_ptr<OA::NestedSCR> tarj,
		  OA::OA_ptr<OA::CFG::CFGInterface> cfg, 
		  OA::RIFG::NodeId fgNode, bool irrIvalIsLoop)
{
  OA::OA_ptr<OA::RIFG> rifg = tarj->getRIFG();
  OA::OA_ptr<OA::CFG::NodeInterface> bb = 
    rifg->getNode(fgNode).convert<OA::CFG::NodeInterface>();
  binutils::Insn* insn = banal::OA_CFG_getBegInsn(bb);
  VMA begVMA = (insn) ? insn->opVMA() : 0;
  
  DIAG_DevMsg(10, "BuildLoopAndStmts: " << bb << " [id: " << bb->getId() << "] " << hex << begVMA << " --> " << enclosingStrct << dec << " " << enclosingStrct->toString_id());

  Struct::ACodeNode* childScope = enclosingStrct;

  OA::NestedSCR::Node_t ity = tarj->getNodeType(fgNode);
  if (ity == OA::NestedSCR::NODE_ACYCLIC 
      || ity == OA::NestedSCR::NODE_NOTHING) {
    // -----------------------------------------------------
    // ACYCLIC: No loops
    // -----------------------------------------------------
  }
  else if (ity == OA::NestedSCR::NODE_INTERVAL || 
	   (irrIvalIsLoop && ity == OA::NestedSCR::NODE_IRREDUCIBLE)) {
    // -----------------------------------------------------
    // INTERVAL or IRREDUCIBLE as a loop: Loop head
    // -----------------------------------------------------
    string fnm, pnm;
    SrcFile::ln line;
    FindLoopBegLineInfo(p, bb, fnm, pnm, line);
    pnm = GetBestFuncName(pnm);
    
    Struct::Loop* loop = new Struct::Loop(NULL, line, line);
    loop->vmaSet().insert(begVMA, begVMA + 1); // a loop id
    locMgr.locate(loop, enclosingStrct, fnm, pnm, line);
    childScope = loop;
  }
  else if (!irrIvalIsLoop && ity == OA::NestedSCR::NODE_IRREDUCIBLE) {
    // -----------------------------------------------------
    // IRREDUCIBLE as no loop: May contain loops
    // -----------------------------------------------------
  }
  else {
    DIAG_Die("Should never reach!");
  }

  // -----------------------------------------------------
  // Process instructions within BB
  // -----------------------------------------------------
  BuildStmts(locMgr, childScope, p, bb);

  return childScope;
}


// BuildStmts: Form loop structure by setting bounds and adding
// statements from the current basic block to 'enclosingStrct'.  Does
// not add duplicates.
static int 
BuildStmts(bloop::LocationMgr& locMgr,
	   Struct::ACodeNode* enclosingStrct, binutils::Proc* p,
	   OA::OA_ptr<OA::CFG::NodeInterface> bb)
{
  static int call_sortId = 0;

  OA::OA_ptr<OA::CFG::NodeStatementsIteratorInterface> it =
    bb->getNodeStatementsIterator();
  for ( ; it->isValid(); ) {
    binutils::Insn* insn = IRHNDL_TO_TY(it->current(), binutils::Insn*);
    VMA vma = insn->vma();
    ushort opIdx = insn->opIndex();
    VMA opvma = insn->opVMA();
    
    // advance iterator [needed when creating VMA interval]
    ++(*it);

    // 1. gather source code info
    string filenm, procnm;
    SrcFile::ln line;
    p->GetSourceFileInfo(vma, opIdx, procnm, filenm, line); 
    procnm = GetBestFuncName(procnm);

    // 2. create a VMA interval
    // the next (or hypothetically next) insn begins no earlier than:
    binutils::Insn* n_insn = (it->isValid()) ? 
      IRHNDL_TO_TY(it->current(), binutils::Insn*) : NULL;
    VMA n_opvma = (n_insn) ? n_insn->opVMA() : insn->endVMA();
    DIAG_Assert(opvma < n_opvma, "Invalid VMAInterval: [" << opvma << ", "
		<< n_opvma << ")");

    VMAInterval vmaint(opvma, n_opvma);

    // 3. Get statement type.  Use a special sort key for calls as a
    // way to protect against bad debugging information which would
    // later cause a call to treated as loop-invariant-code motion and
    // hoisted into a different loop.
    ISA::InsnDesc idesc = insn->desc();
    
    // 4. locate stmt
    Struct::Stmt* stmt = 
      new Struct::Stmt(NULL, line, line, vmaint.beg(), vmaint.end());
    if (idesc.IsSubr()) {
      stmt->sortId(--call_sortId);
    }
    locMgr.locate(stmt, enclosingStrct, filenm, procnm, line);
  }
  return 0;
} 


#if 0 // FIXME: Deprecated
// BuildProcLoopNests: Recursively build loops using Nested SCR
// (Tarjan interval) analysis and returns the number of loops created.
// 
// We visit the interval tree in DFS order which is equivalent to
// visiting each basic block in VMA order.
//
// Note that these loops are UNNORMALIZED.
static int 
BuildProcLoopNests(Struct::ACodeNode* enclosingStrct, binutils::Proc* p, 
		   OA::OA_ptr<OA::NestedSCR> tarj,
		   OA::OA_ptr<OA::CFG::Interface> cfg, 
		   OA::RIFG::NodeId fgNode, 
		   int addStmts, bool irrIvalIsLoop)
{
  int localLoops = 0;
  OA::OA_ptr<OA::RIFG> rifg = tarj->getRIFG();
  OA::OA_ptr<OA::CFG::Interface::Node> bb =
    rifg->getNode(fgNode).convert<OA::CFG::Interface::Node>();
  
  DIAG_DevMsg(50, "BuildProcLoopNests: " << bb <<  " --> "  << hex << enclosingStrct << dec << " " << enclosingStrct->toString_id());

  if (addStmts) {
    // mp->push_back(make_pair(bb, enclosingStrct));
  }
  
  // -------------------------------------------------------
  // Recursively traverse the Nested SCR (Tarjan tree), building loop nests
  // -------------------------------------------------------
  for (int kid = tarj->getInners(fgNode); kid != OA::RIFG::NIL; 
       kid = tarj->getNext(kid) ) {
    OA::OA_ptr<OA::CFG::Interface::Node> bb1 = 
      rifg->getNode(kid).convert<OA::CFG::Interface::Node>();
    OA::NestedSCR::Node_t ity = tarj->getNodeType(kid);
    
    if (ity == OA::NestedSCR::NODE_ACYCLIC) { 
      // -----------------------------------------------------
      // ACYCLIC: No loops
      // -----------------------------------------------------
      if (addStmts) {
	//mp->push_back(make_pair(bb1, enclosingStrct));
      }
    }
    else if (ity == OA::NestedSCR::NODE_INTERVAL || 
	     (irrIvalIsLoop && ity == OA::NestedSCR::NODE_IRREDUCIBLE)) {
      // -----------------------------------------------------
      // INTERVAL or IRREDUCIBLE as a loop: Loop head
      // -----------------------------------------------------

      // add alien context if necessary....
      Struct::Loop* lScope = new Struct::Loop(enclosingStrct, line, line);
      int num = BuildProcLoopNests(lScope, p, tarj, cfg, kid, mp,
				   1, irrIvalIsLoop);
      localLoops += (num + 1);
    }
    else if (!irrIvalIsLoop && ity == OA::NestedSCR::NODE_IRREDUCIBLE) {
      // -----------------------------------------------------
      // IRREDUCIBLE as no loop: May contain loops
      // -----------------------------------------------------
      int num = BuildProcLoopNests(enclosingStrct, p, tarj, cfg, kid, mp,
				   addStmts, irrIvalIsLoop);
      localLoops += num;
    }
    else {
      DIAG_Die("Should never reach!");
    }
  }
  
  return localLoops;
}
#endif


//****************************************************************************
// 
//****************************************************************************

// FindLoopBegLineInfo: Given the head basic block node of the loop,
// find loop begin source line information.  
//
// The routine first attempts to use the source line information for
// the backward branch, if one exists.
//
// Note: It is possible to have multiple or no backward branches
// (e.g. an 'unstructured' loop written with IFs and GOTOs).  In the
// former case, we take the smallest source line of them all; in the
// latter we use headVMA.
static void
FindLoopBegLineInfo(binutils::Proc* p, 
		    OA::OA_ptr<OA::CFG::NodeInterface> headBB,
		    string& begFilenm, string& begProcnm, SrcFile::ln& begLn)
{
  using namespace OA::CFG;

  begLn = SrcFile::ln_NULL;

  // Find the head vma
  binutils::Insn* head = banal::OA_CFG_getBegInsn(headBB);
  VMA headVMA = head->vma();
  ushort headOpIdx = head->opIndex();
  DIAG_Assert(headOpIdx == 0, "Target of a branch at " << headVMA 
	      << " has op-index of: " << headOpIdx);
  
  // Now find the backward branch
  OA::OA_ptr<EdgesIteratorInterface> it 
    = headBB->getCFGIncomingEdgesIterator();
  for ( ; it->isValid(); ++(*it)) {
    OA::OA_ptr<EdgeInterface> e = it->currentCFGEdge();
    
    OA::OA_ptr<NodeInterface> bb = e->getCFGSource();

    binutils::Insn* backBR = banal::OA_CFG_getEndInsn(bb);
    if (!backBR) {
      continue;
    }
    
    VMA vma = backBR->vma();
    ushort opIdx = backBR->opIndex();

    // If we have a backward edge, find the source line of the
    // backward branch.  Note: back edges are not always labeled as such!
    if (e->getType() == BACK_EDGE || vma >= headVMA) {
      SrcFile::ln line;
      string filenm, procnm;
      p->GetSourceFileInfo(vma, opIdx, procnm, filenm, line); 
      if (SrcFile::isValid(line) 
	  && (!SrcFile::isValid(begLn) || line < begLn)) {
	begLn = line;
	begFilenm = filenm;
	begProcnm = procnm;
      }
    }
  }
  
  if (!SrcFile::isValid(begLn)) {
    VMA headOpIdx = head->opIndex();
    p->GetSourceFileInfo(headVMA, headOpIdx, begProcnm, begFilenm, begLn);
  }
}



#if 0
// FindOrCreateStmtNode: Build a Struct::Stmt.  Unlike LocateStmt,
// assumes that procedure boundaries do not need to be expanded.
static Struct::Stmt*
FindOrCreateStmtNode(std::map<SrcFile::ln, Struct::Stmt*>& stmtMap,
		     Struct::ACodeNode* enclosingStrct, SrcFile::ln line, VMAInterval& vmaint)
{
  Struct::Stmt* stmt = stmtMap[line];
  if (!stmt) {
    stmt = new Struct::Stmt(enclosingStrct, line, line, 
			      vmaint.beg(), vmaint.end());
    stmtMap.insert(make_pair(line, stmt));
  }
  else {
    stmt->vmaSet().insert(vmaint); // expand VMA range
  }
  return stmt;
}
#endif

} // namespace bloop

} // namespace banal


//****************************************************************************
// Helpers for normalizing a scope tree
//****************************************************************************

namespace banal {

namespace bloop {

// RemoveOrphanedProcedureRepository: Remove the OrphanedProcedureFile
// from the tree.
static bool 
RemoveOrphanedProcedureRepository(Prof::Struct::Tree* strctTree)
{
  bool changed = false;
  
  Struct::Pgm* pgmStrct = strctTree->GetRoot();
  if (!pgmStrct) { return changed; }
  
  for (Struct::ANodeIterator it(pgmStrct, 
				&Struct::ANodeTyFilter[Struct::ANode::TyFILE]); 
       it.Current(); /* */) {
    Struct::File* file = dynamic_cast<Struct::File*>(it.Current());
    it++; // advance iterator -- it is pointing at 'file'
    
    if (file->name() == OrphanedProcedureFile) {
      file->Unlink(); // unlink 'file' from tree
      delete file;
      changed = true;
    }
  } 
  
  return changed;
}


//****************************************************************************

static bool 
MergeBogusAlienStrct(Struct::ACodeNode* node, Struct::File* file);


static bool 
MergeBogusAlienStrct(Prof::Struct::Tree* strctTree)
{
  bool changed = false;
  
  Struct::Pgm* pgmStrct = strctTree->GetRoot();
  if (!pgmStrct) { return changed; }
  
  for (Struct::ANodeIterator it(pgmStrct, 
				&Struct::ANodeTyFilter[Struct::ANode::TyPROC]);
       it.Current(); ++it) {
    Struct::Proc* proc = dynamic_cast<Struct::Proc*>(it.Current());
    Struct::File* file = proc->AncFile();
    changed |= MergeBogusAlienStrct(proc, file);
  }
  
  return changed;
}


static bool 
MergeBogusAlienStrct(Struct::ACodeNode* node, Struct::File* file)
{
  bool changed = false;
  
  if (!node) { return changed; }

  for (Struct::ACodeNodeChildIterator it(node); it.Current(); /* */) {
    Struct::ACodeNode* child = it.CurACodeNode();
    it++; // advance iterator -- it is pointing at 'child'
    
    // 1. Recursively do any merging for this tree's children
    changed |= MergeBogusAlienStrct(child, file);
    
    // 2. Merge an alien node if it is redundant with its calling context
    if (child->Type() == Struct::ANode::TyALIEN) {
      Struct::Alien* alien = dynamic_cast<Struct::Alien*>(child);
      Struct::ACodeNode* parent = alien->ACodeNodeParent();
      
      Struct::ACodeNode* callCtxt = parent->AncCallingCtxt();
      const string& callCtxtFnm = (callCtxt->Type() == Struct::ANode::TyALIEN) ?
	dynamic_cast<Struct::Alien*>(callCtxt)->fileName() : file->name();
      
      // FIXME: Looking at this again, don't we know that 'callCtxtFnm' is alien?
      if (alien->fileName() == callCtxtFnm
	  && ctxtNameEqFuzzy(callCtxt, alien->name())
	  && LocationMgr::containsIntervalFzy(parent, alien->begLine(), 
					      alien->endLine()))  {
	// Move all children of 'alien' into 'parent'
	changed = Struct::ANode::Merge(parent, alien);
	DIAG_Assert(changed, "MergeBogusAlienStrct: merge failed.");
      }
    }
  }
  
  return changed;
}


//****************************************************************************

// CoalesceDuplicateStmts: Coalesce duplicate statement instances that
// may appear in the scope tree.  There are two basic cases:
//
// Case 1a:
// If the same statement exists at multiple levels within a loop nest,
//   discard all but the innermost instance.
// Rationale: Compiler optimizations may hoist loop-invariant
//   operations out of inner loops.  Note however that in rare cases,
//   code may be moved deeper into a nest (e.g. to free registers).
//   Even so, we want to associate statements within loop nests to the
//   deepest level in which they appear.
// E.g.: lca --- ... --- s1
//          \--- s2
//
// Case 1b:
// If the same statement exists within the same loop, discard all but one.
// Rationale: Multiple statements exist at the same level because of
//   multiple basic blocks containing the same statement, cf. BuildStmts(). [FIXME -- LocationMgr]
//   Also, the merging in case 2 may result in duplicate statements.
// E.g.: lca --- s1
//          \--- s2
//
// Case 2: 
// If duplicate statements appear in different loops, find the least
//   common ancestor (deepest nested common ancestor) in the scope tree
//   and merge the corresponding loops along the paths to each of the
//   duplicate statements.  Note that merging can create instances of
//   case 1.
// Rationale: Compiler optimizations such as loop unrolling (start-up,
//   steady-state, wind-down), e.g., can produce multiple statement
//   instances.
// E.g.: lca ---...--- s1
//          \---...--- s2
static bool CDS_unsafeNormalizations = true;

static bool
CoalesceDuplicateStmts(Struct::ACodeNode* scope, SortIdToStmtMap* stmtMap, 
		       Struct::ANodeSet* visited, Struct::ANodeSet* toDelete,
		       int level);

static bool
CDS_Main(Struct::ACodeNode* scope, SortIdToStmtMap* stmtMap, 
	 Struct::ANodeSet* visited, Struct::ANodeSet* toDelete, int level);

static bool
CDS_InspectStmt(Struct::Stmt* stmt1, SortIdToStmtMap* stmtMap, 
		Struct::ANodeSet* toDelete, int level);

static bool 
CDS_ScopeFilter(const Struct::ANode& x, long type)
{
  return (x.Type() == Struct::ANode::TyPROC 
	  || x.Type() == Struct::ANode::TyALIEN);
}

static bool
CoalesceDuplicateStmts(Prof::Struct::Tree* strctTree, 
		       bool unsafeNormalizations)
{
  bool changed = false;
  CDS_unsafeNormalizations = unsafeNormalizations;
  Struct::Pgm* pgmStrct = strctTree->GetRoot();
  SortIdToStmtMap stmtMap;    // line to statement data map
  Struct::ANodeSet visitedScopes; // all children of a scope have been visited
  Struct::ANodeSet toDelete;      // nodes to delete

  // Apply the normalization routine to each Struct::Proc and Struct::Alien
  // so that 1) we are guaranteed to only process Struct::ACodeNodes and 2) we
  // can assume that all line numbers encountered are within the same
  // file (keeping the SortIdToStmtMap simple and fast).  (Children
  // Struct::Alien's are skipped.)

  Struct::ANodeFilter filter(CDS_ScopeFilter, "CDS_ScopeFilter", 0);
  for (Struct::ANodeIterator it(pgmStrct, &filter); it.Current(); ++it) {
    Struct::ACodeNode* scope = dynamic_cast<Struct::ACodeNode*>(it.Current());
    changed |= CoalesceDuplicateStmts(scope, &stmtMap, &visitedScopes,
				      &toDelete, 1);
    stmtMap.clear();           // Clear statement table
    visitedScopes.clear();     // Clear visited set
    DeleteContents(&toDelete); // Clear 'toDelete'
  }

  return changed;
}


// CoalesceDuplicateStmts Helper: 
//
// Because instances of case 2 can create instances of case 1, we need
// to restart the algorithm at the lca after merging has been applied.
//
// This has some implications:
// - We can delete nodes during the merging. (Deletion of nodes will
//   invalidate the current *and* ancestor iterators out to the
//   lca!)
// - We cannot simply delete nodes in case 1.  The restarting means
//   that nodes that we have already seen are in the statement map and
//   could therefore be deleted out from under us during iteration. 
//   Consider this snippet of code:
// 
//   <L b=... >  // level 3
//     ...
//     <L b="1484" e="1485">  // level 4
//        <S b="1484" />
//        <S b="1484" />
//        <S b="1485" />
//        <S b="1485" />
//     </L>
//     <S b="1485">
//     ...
//   </L>
//   
//   CDS_Main loops on the children of the current scope.  It saves the
//   current value of the iterator to 'child' and then increments it (an
//   attempt to prevet this problem!). After that it recursively calls CDS
//   on 'child'.
//   
//   When 'child' points to the 4th level loop in the picture above, the
//   iterator has been incremented and points to the next child which
//   happens to be a statement for line 1485.
//   
//   When CDS processes the inner loop and reaches a statement for line
//   1485, it realizes a duplicate was found in the level 3 loop -- the
//   statement to which level 3 loop iterator points to.  Because the
//   current statement is on a deeper level (4 as opposed to 3 in the map),
//   the statement on level 3 is unlinked and deleted.  After the level 4
//   loop is completely processed, it returns back to the level 3 loop
//   where it points to a block of memory that was freed already.
//
// Solution: After application of case 2, unwind the recursion stack
//   and restart the algorithm at the least common ancestor.  Retain a
//   set of visited nodes so that we do not have to revisit fully
//   explored and unchanged nodes.  Since the most recently merged
//   path will not be in the visited set, it will be propertly visited
//
// Notes: 
// - We always merge *into* the current path to avoid deleting all nodes
//   on the current recursion stack.
// - Note that the merging of Struct::ACodeNodes can trigger a reordering based
//   on begin/end lines; new children will not simply end up at the
//   end of the child list.
//
// Another solution: Divide into two distinct phases.  Phase 1 collects all
//   statements into a multi-map (that handles sorted iteration and
//   fast local resorts).  Phase 2 iterates over the map, applying
//   case 1 and 2 until all duplicate entries are removed.
static bool
CoalesceDuplicateStmts(Struct::ACodeNode* scope, SortIdToStmtMap* stmtMap, 
		       Struct::ANodeSet* visited, Struct::ANodeSet* toDelete, 
		       int level)
{
  try {
    return CDS_Main(scope, stmtMap, visited, toDelete, level);
  } 
  catch (CDS_RestartException& x) {
    // Unwind the recursion stack until we find the node
    if (x.GetNode() == scope) {
      return CoalesceDuplicateStmts(x.GetNode(), stmtMap, visited, 
				    toDelete, level);
    } 
    else {
      throw;
    }
  }
}


// CDS_Main: Helper for the above. Assumes that all statement line
// numbers are within the same file.  We operate on the children of
// 'scope' to support support node deletion (case 1 above).  When we
// have visited all children of 'scope' we place it in 'visited'.
static bool
CDS_Main(Struct::ACodeNode* scope, SortIdToStmtMap* stmtMap, Struct::ANodeSet* visited, 
	 Struct::ANodeSet* toDelete, int level)
{
  bool changed = false;
  
  if (!scope) { return changed; }
  if (visited->find(scope) != visited->end()) { return changed; }
  if (toDelete->find(scope) != toDelete->end()) { return changed; }

  // A post-order traversal of this node (visit children before parent)...
  for (Struct::ANodeChildIterator it(scope); it.Current(); ++it) {
    Struct::ACodeNode* child = dynamic_cast<Struct::ACodeNode*>(it.Current()); // always true
    DIAG_Assert(child, "");
    
    if (toDelete->find(child) != toDelete->end()) { continue; }
    
    if (child->Type() == Struct::ANode::TyALIEN) { continue; }
    
    DIAG_DevMsgIf(DBG_CDS, "CDS: " << child);

    // 1. Recursively perform re-nesting on 'child'.
    changed |= CoalesceDuplicateStmts(child, stmtMap, visited, toDelete,
				      level + 1);
    
    // 2. Examine 'child'
    if (child->Type() == Struct::ANode::TySTMT) {
      // Note: 'child' may be deleted or a restart exception may be thrown
      Struct::Stmt* stmt = dynamic_cast<Struct::Stmt*>(child);
      changed |= CDS_InspectStmt(stmt, stmtMap, toDelete, level);
    } 
  }
  
  visited->insert(scope);
  return changed; 
}

  
// CDS_InspectStmt: applies case 1 or 2, as described above
static bool
CDS_InspectStmt(Struct::Stmt* stmt1, SortIdToStmtMap* stmtMap, 
		Struct::ANodeSet* toDelete, int level)
{
  bool changed = false;
  
  int sortid = stmt1->sortId();
  StmtData* stmtdata = (*stmtMap)[sortid];
  if (stmtdata) {
    
    Struct::Stmt* stmt2 = stmtdata->GetStmt();
    DIAG_DevMsgIf(DBG_CDS, " Find: " << stmt1 << " " << stmt2);
    
    // Ensure we have two different instances of the same sortid (line)
    if (stmt1 == stmt2) { return false; }
    
    // Find the least common ancestor.  At most it should be a
    // procedure scope.
    Struct::ANode* lca = Struct::ANode::LeastCommonAncestor(stmt1, stmt2);
    DIAG_Assert(lca, "");
    
    // Because we have the lca and know that the descendent nodes are
    // statements (leafs), the test for case 1 is very simple:
    bool case1 = (stmt1->Parent() == lca || stmt2->Parent() == lca);
    if (case1) {
      // Case 1: Duplicate statements. Delete shallower one.
      Struct::Stmt* toRemove = NULL;
      
      if (stmtdata->GetLevel() < level) { // stmt2.level < stmt1.level
	toRemove = stmt2;
	stmtdata->SetStmt(stmt1);  // replace stmt2 with stmt1
	stmtdata->SetLevel(level);
      } 
      else { 
	toRemove = stmt1;
      }
      
      toDelete->insert(toRemove);
      stmtdata->GetStmt()->vmaSet().merge(toRemove->vmaSet()); // merge VMAs
      DIAG_DevMsgIf(DBG_CDS, "  Delete: " << toRemove);
      changed = true;
    } 
    else if (CDS_unsafeNormalizations) {
      // Case 2: Duplicate statements in different loops (or scopes).
      // Merge the nodes from stmt2->lca into those from stmt1->lca.
      DIAG_DevMsgIf(DBG_CDS, "  Merge: " << stmt1 << " <- " << stmt2);
      changed = Struct::ANode::MergePaths(lca, stmt1, stmt2);
      if (changed) {
	// We may have created instances of case 1.  Furthermore,
	// while neither statement is deleted ('stmtMap' is still
	// valid), iterators between stmt1 and 'lca' are invalidated.
	// Restart at lca.
	Struct::ACodeNode* lca_CI = dynamic_cast<Struct::ACodeNode*>(lca);
	DIAG_Assert(lca_CI, "");
	throw CDS_RestartException(lca_CI);
      }
    }
  } 
  else {
    // Add the statement instance to the map
    stmtdata = new StmtData(stmt1, level);
    (*stmtMap)[sortid] = stmtdata;
    DIAG_DevMsgIf(DBG_CDS, " Map: " << stmt1);
  }
  
  return changed;
}


//****************************************************************************

// MergePerfectlyNestedLoops: Fuse together a child with a parent when
// the child is perfectly nested in the parent.
static bool 
MergePerfectlyNestedLoops(Struct::ANode* node);


static bool 
MergePerfectlyNestedLoops(Prof::Struct::Tree* strctTree)
{
  return MergePerfectlyNestedLoops(strctTree->GetRoot());
}


static bool 
MergePerfectlyNestedLoops(Struct::ANode* node)
{
  bool changed = false;
  
  if (!node) { return changed; }
  
  // A post-order traversal of this node (visit children before parent)...
  for (Struct::ANodeChildIterator it(node); it.Current(); /* */) {
    Struct::ACodeNode* child = dynamic_cast<Struct::ACodeNode*>(it.Current()); // always true
    DIAG_Assert(child, "");
    it++; // advance iterator -- it is pointing at 'child'
    
    // 1. Recursively do any merging for this tree's children
    changed |= MergePerfectlyNestedLoops(child);
    
    // 2. Test if this is a perfectly nested loop with identical loop
    //    bounds and merge if so.

    // The following cast may be illegal but the 'perfect nesting' test
    //   will ensure it doesn't cause a problem (Loops are Struct::ACodeNode's).
    // Perfectly nested test: child is a loop; parent is a loop; and
    //   this is only child.  
    Struct::ACodeNode* n_CI = dynamic_cast<Struct::ACodeNode*>(node); 
    bool perfNested = (child->Type() == Struct::ANode::TyLOOP &&
		       node->Type() == Struct::ANode::TyLOOP &&
		       node->ChildCount() == 1);
    if (perfNested && SrcFile::isValid(child->begLine(), child->endLine()) &&
	child->begLine() == n_CI->begLine() &&
	child->endLine() == n_CI->endLine()) { 

      // Move all children of 'child' so that they are children of 'node'
      changed = Struct::ANode::Merge(node, child);
      DIAG_Assert(changed, "MergePerfectlyNestedLoops: merge failed.");
    }
  } 
  
  return changed; 
}


//****************************************************************************

// RemoveEmptyNodes: Removes certain 'empty' scopes from the tree,
// always maintaining the top level Struct::Pgm (PGM) scope.  See the
// predicate 'RemoveEmptyNodes_isEmpty' for details.
static bool 
RemoveEmptyNodes(Struct::ANode* node);

static bool 
RemoveEmptyNodes_isEmpty(const Struct::ANode* node);


static bool 
RemoveEmptyNodes(Prof::Struct::Tree* strctTree)
{
  // Always maintain the top level PGM scope, even if empty
  return RemoveEmptyNodes(strctTree->GetRoot());
}


static bool 
RemoveEmptyNodes(Struct::ANode* node)
{
  bool changed = false;
  
  if (!node) { return changed; }

  // A post-order traversal of this node (visit children before parent)...
  for (Struct::ANodeChildIterator it(node); it.Current(); /* */) {
    Struct::ANode* child = dynamic_cast<Struct::ANode*>(it.Current());
    it++; // advance iterator -- it is pointing at 'child'
    
    // 1. Recursively do any trimming for this tree's children
    changed |= RemoveEmptyNodes(child);

    // 2. Trim this node if necessary
    if (RemoveEmptyNodes_isEmpty(child)) {
      child->Unlink(); // unlink 'child' from tree
      delete child;
      changed = true;
    }
  } 
  
  return changed; 
}


// RemoveEmptyNodes_isEmpty: determines whether a scope is 'empty':
//   true, if a Struct::File has no children.
//   true, if a Struct::Loop or Struct::Proc has no children *and* an empty
//     VMAIntervalSet.
//   false, otherwise
static bool 
RemoveEmptyNodes_isEmpty(const Struct::ANode* node)
{
  if ((node->Type() == Struct::ANode::TyFILE 
       || node->Type() == Struct::ANode::TyALIEN)
      && node->ChildCount() == 0) {
    return true;
  }
  if ((node->Type() == Struct::ANode::TyPROC 
       || node->Type() == Struct::ANode::TyLOOP)
      && node->ChildCount() == 0) {
    const Struct::ACodeNode* n = dynamic_cast<const Struct::ACodeNode*>(node);
    return n->vmaSet().empty();
  }
  return false;
}


//****************************************************************************

// FilterFilesFromStrctTree: 
static bool 
FilterFilesFromStrctTree(Prof::Struct::Tree* strctTree, 
			 const char* canonicalPathList)
{
  bool changed = false;
  
  Struct::Pgm* pgmStrct = strctTree->GetRoot();
  if (!pgmStrct) { return changed; }
  
  for (Struct::ANodeIterator it(pgmStrct, 
				&Struct::ANodeTyFilter[Struct::ANode::TyFILE]); 
       it.Current(); /* */) {
    Struct::File* file = dynamic_cast<Struct::File*>(it.Current());
    it++; // advance iterator -- it is pointing at 'file'
    
    // Verify this file in the current list of acceptible paths
    string baseFileName = FileUtil::basename(file->name());
    DIAG_Assert(!baseFileName.empty(), "Invalid path!");
    if (!pathfind(canonicalPathList, baseFileName.c_str(), "r")) {
      file->Unlink(); // unlink 'file' from tree
      delete file;
      changed = true;
    }
  } 
  
  return changed;
}


//****************************************************************************
// Helpers for traversing the Nested SCR (Tarjan Tree)
//****************************************************************************

void
SortIdToStmtMap::clear()
{
  for (iterator it = begin(); it != end(); ++it) {
    delete (*it).second;
  }
  My_t::clear();
}

//****************************************************************************
// Helper Routines
//****************************************************************************

static void 
DeleteContents(Struct::ANodeSet* s)
{
  // Delete nodes in toDelete
  for (Struct::ANodeSet::iterator it = s->begin(); it != s->end(); ++it) {
    Struct::ANode* n = (*it);
    n->Unlink(); // unlink 'n' from tree
    delete n;
  }
  s->clear();
}


} // namespace bloop

} // namespace banal

