/*-------------------------------------------------------------------------
 *
 * nodeHashjoin.c
 *	  Routines to handle hash join nodes
 *
 * Portions Copyright (c) 1996-2005, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/executor/nodeHashjoin.c,v 1.75.2.3 2005/11/28 23:46:24 tgl Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "executor/executor.h"
#include "executor/hashjoin.h"
#include "executor/nodeHash.h"
#include "executor/nodeHashjoin.h"
#include "optimizer/clauses.h"
#include "utils/memutils.h"


static TupleTableSlot *ExecHashJoinOuterGetTuple(PlanState *outerNode,
						  HashJoinState *hjstate,
						  uint32 *hashvalue);
static TupleTableSlot *ExecHashJoinGetSavedTuple(HashJoinState *hjstate,
						  BufFile *file,
						  uint32 *hashvalue,
						  TupleTableSlot *tupleSlot);
static int	ExecHashJoinNewBatch(HashJoinState *hjstate);

/* ----------------------------------------------------------------
 *		ExecHashJoin
 *
 *		This function implements the Hybrid Hashjoin algorithm.
 *
 *		Note: the relation we build hash table on is the "inner"
 *			  the other one is "outer".
 * ----------------------------------------------------------------
 */
TupleTableSlot *				/* return: a tuple or NULL */
ExecHashJoin(HashJoinState *node)
{
	EState	   *estate;
	HashState  *outerHashNode; //CSI3130
	HashState  *innerHashNode; //CSI3130
	List	   *joinqual;
	List	   *otherqual;

	TupleTableSlot *inntuple;
	TupleTableSlot *outtuple; //CSI3130
	ExprContext *econtext;
	ExprDoneCond isDone;
	HashJoinTable innerHashtable; //CSI3130
	HashJoinTable outerHashtable; //CSI3130
	
	HeapTuple	curtuple;
	TupleTableSlot *innerTupleSlot; //CSI3130
	TupleTableSlot *outerTupleSlot;
	uint32	hashvalue;
	int	batchno;

	/*
	 * get information from HashJoin node
	 */
	estate = node->js.ps.state;
	joinqual = node->js.joinqual;
	otherqual = node->js.ps.qual;
	innerHashNode = (HashState *) innerPlanState(node); //CSI3130
	outerHashNode = (HashState *) outerPlanState(node); //CSI3130

	/*
	 * get information from HashJoin state
	 */
	innerHashtable = node->hj_InnerHashTable; //CSI3130
	outerHashtable = node->hj_OuterHashTable; //CSI3130
	econtext = node->js.ps.ps_ExprContext;

	/*
	 * Check to see if we're still projecting out tuples from a previous join
	 * tuple (because there is a function-returning-set in the projection
	 * expressions).  If so, try to project another one.
	 */
	
	/* CSI 3130 COMMENTED OUT ORIGINAL CODE
	if (node->js.ps.ps_TupFromTlist)
	{
		TupleTableSlot *result;

		result = ExecProject(node->js.ps.ps_ProjInfo, &isDone);
		if (isDone == ExprMultipleResult)
			return result;
	
		node->js.ps.ps_TupFromTlist = false;
	} 
	*/

	/*
	 * If we're doing an IN join, we want to return at most one row per outer
	 * tuple; so we can stop scanning the inner scan if we matched on the
	 * previous try.
	 */
	if (node->js.jointype == JOIN_IN && node->hj_MatchedOuter)
		node->hj_NeedNewOuter = true;

	/*
	 * Reset per-tuple memory context to free any expression evaluation
	 * storage allocated in the previous tuple cycle.  Note this can't happen
	 * until we're done projecting out tuples from a join tuple.
	 */
	ResetExprContext(econtext);

	/*
	 * if this is the first call, build the hash table for inner relation
	 */
	if (innerHashtable == NULL && outerHashtable == NULL) //CSI3130
	{
		/*
		 * If the outer relation is completely empty, we can quit without
		 * building the hash table.  However, for an inner join it is only a
		 * win to check this when the outer relation's startup cost is less
		 * than the projected cost of building the hash table.	Otherwise it's
		 * best to build the hash table first and see if the inner relation is
		 * empty.  (When it's an outer join, we should always make this check,
		 * since we aren't going to be able to skip the join on the strength
		 * of an empty inner relation anyway.)
		 *
		 * If we are rescanning the join, we make use of information gained
		 * on the previous scan: don't bother to try the prefetch if the
		 * previous scan found the outer relation nonempty.  This is not
		 * 100% reliable since with new parameters the outer relation might
		 * yield different results, but it's a good heuristic.
		 *
		 * The only way to make the check is to try to fetch a tuple from the
		 * outer plan node.  If we succeed, we have to stash it away for later
		 * consumption by ExecHashJoinOuterGetTuple.
		 */
		/* CSI3130 COMMENTED OUT ORIGINAL CODE
		if (node->js.jointype == JOIN_LEFT ||
			(outerHashNode->ps.plan->startup_cost < innerHashNode->ps.plan->total_cost &&
			 !node->hj_OuterNotEmpty))
		{
			node->hj_FirstOuterTupleSlot = ExecProcNode((PlanState *)outerHashNode);
			if (TupIsNull(node->hj_FirstOuterTupleSlot))
			{
				node->hj_OuterNotEmpty = false;
				return NULL;
			}
			else
				node->hj_OuterNotEmpty = true;
		}
		else
			node->hj_FirstOuterTupleSlot = NULL; 
		*/

		/*
		 * create the hash table
		 */
		innerHashtable = ExecHashTableCreate((Hash *) innerHashNode->ps.plan, node->hj_HashOperators);
		node->hj_InnerHashTable = innerHashtable;

        outerHashtable = ExecHashTableCreate((Hash *) outerHashNode->ps.plan, node->hj_HashOperators); //CSI3130
        node->hj_OuterHashTable = outerHashtable; //CSI3130


		/*
		 * execute the Hash node, to build the hash table
		 */
		innerHashNode->hashtable = innerHashtable; //CSI3130
		outerHashNode->hashtable = outerHashtable; //CSI3130

		/* (void) MultiExecProcNode((PlanState *) hashNode); */

		/*
		 * If the inner relation is completely empty, and we're not doing an
		 * outer join, we can quit without scanning the outer relation.
		 */
		/* if (hashtable->totalTuples == 0 && node->js.jointype != JOIN_LEFT)
			return NULL;*/
		/*
		 * need to remember whether nbatch has increased since we began
		 * scanning the outer relation
		 */
		/* hashtable->nbatch_outstart = hashtable->nbatch; */

		/*
		 * Reset OuterNotEmpty for scan.  (It's OK if we fetched a tuple
		 * above, because ExecHashJoinOuterGetTuple will immediately
		 * set it again.)
		 */
		 /* node->hj_OuterNotEmpty = false; */
	}

	/*
	 * CSI3130 run the hash join process
	 */
	for (;;) {
		if (!node->hj_innerExhausted || !node->hj_outerExhausted) {
			if (node->hj_innerExhausted) {
				node->hj_fetchingFromInner = false;
			}
			if (node->hj_outerExhausted){
				node->hj_fetchingFromInner = true;
			}
			if (node->hj_fetchingFromInner) {
				elog(WARNING, "Looking for next Inner");
			} else {
				elog(WARNING, "Looking for next Outer");
			}

			// Try the next inner tuple
			if (!node->hj_innerExhausted && node->hj_NeedNewInner && node->hj_fetchingFromInner) {
				innerTupleSlot = ExecProcNode((PlanState *) innerHashNode);
				node->js.ps.ps_InnerTupleSlot = innerTupleSlot;	
				
				if (!TupIsNull(innerTupleSlot)) {
					elog(WARNING,"processing next inner tuple");
					node->hj_NeedNewInner = false;
					
					// Set the econtext, a lot of functions can only see the tuple if put into econtext
					econtext = node->js.ps.ps_ExprContext;
					econtext->ecxt_innertuple = innerTupleSlot;

					// Get hash values of inner tuple
					hashvalue = ExecHashGetHashValue(outerHashtable, econtext, node->hj_InnerHashKeys);
					elog(WARNING, "computed the hash value of next inner tuple");
					
					// Reset the current inner tuple
					node->js.ps.ps_InnerTupleSlot = innerTupleSlot;
					// Set the econtext
					econtext->ecxt_innertuple = node->js.ps.ps_InnerTupleSlot;

					// Find corresponding bucket
					node->hj_InnerCurHashValue = hashvalue;
					ExecHashGetBucketAndBatch(outerHashtable, hashvalue, &node->hj_OuterCurBucketNo, &batchno); // &batchno -> 0 or 1
					node->hj_OuterCurTuple = NULL;	
					elog(WARNING, "set outer current tuple to NULL");			
				} else {
					elog(WARNING,"inner exhausted");
					node->hj_innerExhausted = true;	
				}
			}
	
			// Try the next outer tuple very similar to above   
			if (!node->hj_outerExhausted && node->hj_NeedNewOuter  && !node->hj_fetchingFromInner) {
            	outerTupleSlot = ExecProcNode((PlanState *) outerHashNode);
                node->js.ps.ps_OuterTupleSlot = outerTupleSlot;

                if (!TupIsNull(outerTupleSlot)) {
                    elog(WARNING, "processing next outer tuple");
                    node->hj_NeedNewOuter = false;

                    // Set the econtext, a lot of functions can only see the tuple if put into econtext
                    econtext = node->js.ps.ps_ExprContext;
                    econtext->ecxt_outertuple = outerTupleSlot;

                    // Get hash values of inner tuple
                    hashvalue = ExecHashGetHashValue(innerHashtable, econtext, node->hj_OuterHashKeys);
					elog(WARNING, "computed the hash value of next outer tuple");

                    // Reset the current outer tuple
                    node->js.ps.ps_OuterTupleSlot = outerTupleSlot;
                    econtext->ecxt_outertuple = node->js.ps.ps_OuterTupleSlot;

                    // Find corresponding bucket
                    node->hj_OuterCurHashValue = hashvalue;
                    ExecHashGetBucketAndBatch(innerHashtable, hashvalue, &node->hj_InnerCurBucketNo, &batchno); // &batchno -> 0 or 1
                    node->hj_InnerCurTuple = NULL;
					elog(WARNING,"set inner current tuple to NULL");		
                } else {
                    elog(WARNING,"outer exhausted");
                    node->hj_outerExhausted = true;
                }
			}
		
			// Check if both outer and inner tables are empty	
			if (node->hj_innerExhausted && node->hj_outerExhausted) {
				elog(WARNING, "outer AND inner nodes are exhausted");
				return NULL;
			}
	
			// Probe inner table for matches
			if (!TupIsNull(node->js.ps.ps_InnerTupleSlot) && node->hj_fetchingFromInner) {
				elog(WARNING,"probe for inner tuple matches");
	 			for (;;) {
					ExprContext *econtext = node->js.ps.ps_ExprContext;
					econtext->ecxt_innertuple = node->js.ps.ps_InnerTupleSlot;
					curtuple = ExecScanHashBucket(node, econtext);
					if (curtuple == NULL) {
						node->hj_fetchingFromInner = false;
						elog(WARNING,"out of inner matches");
						break;
					}
					elog(WARNING,"got a match but still need to test non-hashed quals");

					/*
					 * we've got a match, but still need to test non-hashed quals
					 */
					outtuple = ExecStoreTuple(curtuple, node->hj_OuterTupleSlot, InvalidBuffer, false);	/* don't pfree this tuple */
					econtext->ecxt_outertuple = outtuple;

					/* reset temp memory each time to avoid leaks from qual expr */
					ResetExprContext(econtext);

					/*
					 * if we pass the qual, then save state for next call and have
					 * ExecProject form the projection, store it in the tuple table,
					 * and return the slot.
					 *
					 * Only the joinquals determine MatchedOuter status, but all quals
					 * must pass to actually return the tuple.
					 */
					if (joinqual == NIL || ExecQual(joinqual, econtext, false)) {
						if (otherqual == NIL || ExecQual(otherqual, econtext, false)) {
							TupleTableSlot *result;
							result = ExecProject(node->js.ps.ps_ProjInfo, &isDone);									
							if (isDone != ExprEndResult) {
								node->hj_foundByProbingOuter=node->hj_foundByProbingOuter+1;
								node->js.ps.ps_TupFromTlist = (isDone == ExprMultipleResult);
								elog(WARNING,"**RESULT**");
								return result;
							}
						}
					}
				}
				node->hj_NeedNewInner = true;
				node->js.ps.ps_InnerTupleSlot = NULL;
				node->hj_fetchingFromInner = false;
				continue;
			}

            /* probe for outer tuple matches*/
            if (!TupIsNull(node->js.ps.ps_OuterTupleSlot) && !node->hj_fetchingFromInner) {
 				elog(WARNING,"probe for outer tuple matches");                               
				for (;;) {
                    ExprContext *econtext = node->js.ps.ps_ExprContext;
                    econtext->ecxt_outertuple = node->js.ps.ps_OuterTupleSlot;
                    curtuple = ExecScanHashBucket(node, econtext);
					if (curtuple == NULL) {
 						node->hj_fetchingFromInner = true;
						elog(WARNING,"out of outer matches");
						break;			
					}
					elog(WARNING,"got a match but still need to test non-hashed quals");
                    /*
					 * we've got a match, but still need to test non-hashed quals
					 */
                    inntuple = ExecStoreTuple(curtuple, node->hj_InnerTupleSlot, InvalidBuffer, false); /* don't pfree this tuple */
                    econtext->ecxt_innertuple = inntuple;

                    /* reset temp memory each time to avoid leaks from qual expr */
                    ResetExprContext(econtext);

                    /*
					 * if we pass the qual, then save state for next call and have
					 * ExecProject form the projection, store it in the tuple table,
					 * and return the slot.
					 *
					 * Only the joinquals determine MatchedOuter status, but all quals
					 * must pass to actually return the tuple.
					 */
                    if (joinqual == NIL || ExecQual(joinqual, econtext, false)) {
                        if (otherqual == NIL || ExecQual(otherqual, econtext, false)) {
                            TupleTableSlot *result;
							result = ExecProject(node->js.ps.ps_ProjInfo, &isDone);
                            if (isDone != ExprEndResult) {
                                node->hj_foundByProbingInner = node->hj_foundByProbingInner+1;
                                node->js.ps.ps_TupFromTlist = (isDone == ExprMultipleResult);
								elog(WARNING,"**RESULT**");
                                return result; 
                            }
                        }
                    }
		        }
                node->hj_NeedNewOuter = true;
                node->js.ps.ps_OuterTupleSlot = NULL;
  				node->hj_fetchingFromInner = true;
				continue;
            }
		}
	}
	return NULL;
}

/* ----------------------------------------------------------------
 *		ExecInitHashJoin
 *
 *		Init routine for HashJoin node.
 * ----------------------------------------------------------------
 */
HashJoinState *
ExecInitHashJoin(HashJoin *node, EState *estate)
{
	HashJoinState *hjstate;

    Hash       *outerHashNode; //CSI3130
    Hash       *innerHashNode; //CSI3130
	List	   *lclauses;
	List	   *rclauses;
	List	   *hoperators;
	ListCell   *l;

	/*
	 * create state structure
	*/	
	hjstate = makeNode(HashJoinState);
	hjstate->js.ps.plan = (Plan *) node;
	hjstate->js.ps.state = estate;

	/*
	 * Miscellaneous initialization 
	 * create expression context for node
	 */
	ExecAssignExprContext(estate, &hjstate->js.ps);

	/*
	 * initialize child expressions
	 */
	hjstate->js.ps.targetlist = (List *)
		ExecInitExpr((Expr *) node->join.plan.targetlist,
					 (PlanState *) hjstate);
	hjstate->js.ps.qual = (List *)
		ExecInitExpr((Expr *) node->join.plan.qual,
					 (PlanState *) hjstate);
	hjstate->js.jointype = node->join.jointype;
	hjstate->js.joinqual = (List *)
		ExecInitExpr((Expr *) node->join.joinqual,
					 (PlanState *) hjstate);
	hjstate->hashclauses = (List *)
		ExecInitExpr((Expr *) node->hashclauses,
					 (PlanState *) hjstate);

	/*
	 * initialize child nodes
	 */
    outerHashNode = (Hash *) outerPlan(node); //CSI3130
    innerHashNode = (Hash *) innerPlan(node); //CSI3130

    outerPlanState(hjstate) = ExecInitNode((Plan *) outerHashNode, estate); //CSI3130
    innerPlanState(hjstate) = ExecInitNode((Plan *) innerHashNode, estate); //CSI3130

#define HASHJOIN_NSLOTS 3

	/*
	 * tuple table initialization
	 */
	ExecInitResultTupleSlot(estate, &hjstate->js.ps);
	hjstate->hj_OuterTupleSlot = ExecInitExtraTupleSlot(estate);
	hjstate->hj_InnerTupleSlot = ExecInitExtraTupleSlot(estate); //CSI3130	

	switch (node->join.jointype)
	{
		case JOIN_INNER:
		case JOIN_IN:
			break;
		case JOIN_LEFT:
			hjstate->hj_NullInnerTupleSlot =
			ExecInitNullTupleSlot(estate,
			ExecGetResultType(innerPlanState(hjstate)));
			break;
		default:
			elog(ERROR, "unrecognized join type: %d",
			(int) node->join.jointype);
	}

	/*
	 * now for some voodoo.  our temporary tuple slot is actually the result
	 * tuple slot of the Hash node (which is our inner plan).  we do this
	 * because Hash nodes don't return tuples via ExecProcNode() -- instead
	 * the hash join node uses ExecScanHashBucket() to get at the contents of
	 * the hash table.	-cim 6/9/91
	 */

    {
	    HashState  *hashstate = (HashState *) innerPlanState(hjstate);
	    TupleTableSlot *slot = hashstate->ps.ps_ResultTupleSlot;
	    hjstate->hj_InnerHashTupleSlot = slot;
	    
		hashstate = (HashState *) outerPlanState(hjstate); //CSI3130 
		slot = hashstate->ps.ps_ResultTupleSlot; //CSI3130
		hjstate->hj_OuterHashTupleSlot = slot; //CSI3130
    }

	/*
	 * initialize tuple type and projection info
	 */
	ExecAssignResultTypeFromTL(&hjstate->js.ps);
	ExecAssignProjectionInfo(&hjstate->js.ps);

	ExecSetSlotDescriptor(hjstate->hj_OuterTupleSlot,
						  ExecGetResultType(outerPlanState(hjstate)),
						  false);

	ExecSetSlotDescriptor(hjstate->hj_InnerTupleSlot, 
						  ExecGetResultType(innerPlanState(hjstate)),
						  false); //CSI3130	

	/*
	 * initialize hash-specific info
	 */

	hjstate->hj_InnerHashTable = NULL;
	hjstate->hj_FirstOuterTupleSlot = NULL;
	hjstate->hj_OuterHashTable = NULL; //CSI3130
	hjstate->hj_FirstInnerTupleSlot = NULL; //CSI3130

	hjstate->hj_InnerCurHashValue = 0;
	hjstate->hj_InnerCurBucketNo = 0;
	hjstate->hj_InnerCurTuple = NULL;

    hjstate->hj_OuterCurHashValue = 0; //CSI3130
    hjstate->hj_OuterCurBucketNo = 0; //CSI3130
    hjstate->hj_OuterCurTuple = NULL; //CSI3130
	
	/*
	 * Deconstruct the hash clauses into outer and inner argument values, so
	 * that we can evaluate those subexpressions separately.  Also make a list
	 * of the hash operator OIDs, in preparation for looking up the hash
	 * functions to use.
	 */
	lclauses = NIL;
	rclauses = NIL;
	hoperators = NIL;
	foreach(l, hjstate->hashclauses)
	{
		FuncExprState *fstate = (FuncExprState *) lfirst(l);
		OpExpr	   *hclause;

		Assert(IsA(fstate, FuncExprState));
		hclause = (OpExpr *) fstate->xprstate.expr;
		Assert(IsA(hclause, OpExpr));
		lclauses = lappend(lclauses, linitial(fstate->args));
		rclauses = lappend(rclauses, lsecond(fstate->args));
		hoperators = lappend_oid(hoperators, hclause->opno);
	}
	hjstate->hj_OuterHashKeys = lclauses;
	hjstate->hj_InnerHashKeys = rclauses;
	hjstate->hj_HashOperators = hoperators;

	/* child Hash node needs to evaluate inner hash keys, too */
	((HashState *) innerPlanState(hjstate))->hashkeys = rclauses;
    ((HashState *) outerPlanState(hjstate))->hashkeys = lclauses; //CSI3130

	hjstate->js.ps.ps_OuterTupleSlot = NULL;
	hjstate->js.ps.ps_InnerTupleSlot = NULL; //CSI3130
	hjstate->js.ps.ps_TupFromTlist = false;
	hjstate->hj_NeedNewOuter = true;
	hjstate->hj_NeedNewInner = true; //CSI3130
	hjstate->hj_MatchedOuter = false;

	// CSI3130 COMMENTED OUT ORIGINAL CODE
	/* hjstate->hj_OuterNotEmpty = false;
	   hjstate->hj_InnerNotEmpty = false; */

	hjstate->hj_innerExhausted = false; //CSI3130
	hjstate->hj_outerExhausted = false; //CSI3130
	hjstate->hj_foundByProbingInner = 0; //CSI3130
	hjstate->hj_foundByProbingOuter = 0; //CSI3130
	hjstate->hj_fetchingFromInner = true; //CSI3130

	return hjstate;
}

int
ExecCountSlotsHashJoin(HashJoin *node)
{
	return ExecCountSlotsNode(outerPlan(node)) +
		ExecCountSlotsNode(innerPlan(node)) +
		HASHJOIN_NSLOTS;
}

/* ----------------------------------------------------------------
 *		ExecEndHashJoin
 *
 *		clean up routine for HashJoin node
 * ----------------------------------------------------------------
 */
void
ExecEndHashJoin(HashJoinState *node)
{
	/*
	 * Free hash table
	 */
	if (node->hj_InnerHashTable)
	{
		ExecHashTableDestroy(node->hj_InnerHashTable);
		node->hj_InnerHashTable = NULL;
	}
    if (node->hj_OuterHashTable) //CSI3130
    {
        ExecHashTableDestroy(node->hj_OuterHashTable); //CSI3130
        node->hj_OuterHashTable = NULL; //CSI3130
    }

	/*
	 * Free the exprcontext
	 */
	ExecFreeExprContext(&node->js.ps);

	/*
	 * clean out the tuple table
	 */
	ExecClearTuple(node->js.ps.ps_ResultTupleSlot);
	//ExecClearTuple(node->js.ps.ps_InnerTupleSlot); //CSI3130
	//ExecClearTuple(node->js.ps.ps_OuterTupleSlot); //CSI3130
	ExecClearTuple(node->hj_OuterTupleSlot);
	ExecClearTuple(node->hj_InnerTupleSlot); //CSI3130
	ExecClearTuple(node->hj_InnerHashTupleSlot);
	ExecClearTuple(node->hj_OuterHashTupleSlot); //CSI3130

	/*
	 * clean up subtrees
	 */
	ExecEndNode(outerPlanState(node));
	ExecEndNode(innerPlanState(node));
}

/*
 * ExecHashJoinOuterGetTuple
 *
 *		get the next outer tuple for hashjoin: either by
 *		executing a plan node in the first pass, or from
 *		the temp files for the hashjoin batches.
 *
 * Returns a null slot if no more outer tuples.  On success, the tuple's
 * hash value is stored at *hashvalue --- this is either originally computed,
 * or re-read from the temp file.
 */
static TupleTableSlot *
ExecHashJoinOuterGetTuple(PlanState *outerNode,
						  HashJoinState *hjstate,
						  uint32 *hashvalue)
{
	HashJoinTable hashtable = hjstate->hj_InnerHashTable;
	int			curbatch = hashtable->curbatch;
	TupleTableSlot *slot;

	if (curbatch == 0)
	{							/* if it is the first pass */

		/*
		 * Check to see if first outer tuple was already fetched by
		 * ExecHashJoin() and not used yet.
		 */
		slot = hjstate->hj_FirstOuterTupleSlot;
		if (!TupIsNull(slot))
			hjstate->hj_FirstOuterTupleSlot = NULL;
		else
			slot = ExecProcNode(outerNode);
		if (!TupIsNull(slot))
		{
			/*
			 * We have to compute the tuple's hash value.
			 */
			ExprContext *econtext = hjstate->js.ps.ps_ExprContext;

			econtext->ecxt_outertuple = slot;
			*hashvalue = ExecHashGetHashValue(hashtable, econtext,
											  hjstate->hj_OuterHashKeys);

			/* remember outer relation is not empty for possible rescan */
			/*hjstate->hj_OuterNotEmpty = true; CSI3130*/

			return slot;
		}

		/*
		 * We have just reached the end of the first pass. Try to switch to a
		 * saved batch.
		 */
		curbatch = ExecHashJoinNewBatch(hjstate);
	}

	/*
	 * Try to read from a temp file. Loop allows us to advance to new batches
	 * as needed.  NOTE: nbatch could increase inside ExecHashJoinNewBatch, so
	 * don't try to optimize this loop.
	 */
	while (curbatch < hashtable->nbatch)
	{
		slot = ExecHashJoinGetSavedTuple(hjstate,
										 hashtable->outerBatchFile[curbatch],
										 hashvalue,
										 hjstate->hj_OuterTupleSlot);
		if (!TupIsNull(slot))
			return slot;
		curbatch = ExecHashJoinNewBatch(hjstate);
	}

	/* Out of batches... */
	return NULL;
}

/*
 * ExecHashJoinNewBatch
 *		switch to a new hashjoin batch
 *
 * Returns the number of the new batch (1..nbatch-1), or nbatch if no more.
 * We will never return a batch number that has an empty outer batch file.
 */
static int
ExecHashJoinNewBatch(HashJoinState *hjstate)
{
	/*CSI3130 commented out the function inners, but left definition to watch for dependencies
	HashJoinTable hashtable = hjstate->hj_InnerHashTable;
	int			nbatch;
	int			curbatch;
	BufFile    *innerFile;
	TupleTableSlot *slot;
	uint32		hashvalue;

start_over:
	nbatch = hashtable->nbatch;
	curbatch = hashtable->curbatch;

	if (curbatch > 0)
	{ */
		/*
		 * We no longer need the previous outer batch file; close it right
		 * away to free disk space.
		 */
	/*	if (hashtable->outerBatchFile[curbatch])
			BufFileClose(hashtable->outerBatchFile[curbatch]);
		hashtable->outerBatchFile[curbatch] = NULL;
	}
	*/
	/*
	 * We can always skip over any batches that are completely empty on both
	 * sides.  We can sometimes skip over batches that are empty on only one
	 * side, but there are exceptions:
	 *
	 * 1. In a LEFT JOIN, we have to process outer batches even if the inner
	 * batch is empty.
	 *
	 * 2. If we have increased nbatch since the initial estimate, we have to
	 * scan inner batches since they might contain tuples that need to be
	 * reassigned to later inner batches.
	 *
	 * 3. Similarly, if we have increased nbatch since starting the outer
	 * scan, we have to rescan outer batches in case they contain tuples that
	 * need to be reassigned.
	 */
	/*
 	curbatch++;
	while (curbatch < nbatch &&
		   (hashtable->outerBatchFile[curbatch] == NULL ||
			hashtable->innerBatchFile[curbatch] == NULL))
	{
		if (hashtable->outerBatchFile[curbatch] &&
			hjstate->js.jointype == JOIN_LEFT)
			break;				
		if (hashtable->innerBatchFile[curbatch] &&
			nbatch != hashtable->nbatch_original)
			break;				
		if (hashtable->outerBatchFile[curbatch] &&
			nbatch != hashtable->nbatch_outstart)
			break;			

		if (hashtable->innerBatchFile[curbatch])
			BufFileClose(hashtable->innerBatchFile[curbatch]);
		hashtable->innerBatchFile[curbatch] = NULL;
		if (hashtable->outerBatchFile[curbatch])
			BufFileClose(hashtable->outerBatchFile[curbatch]);
		hashtable->outerBatchFile[curbatch] = NULL;
		curbatch++;
	}

	if (curbatch >= nbatch)
		return curbatch;	

	hashtable->curbatch = curbatch;
*/
	/*
	 * Reload the hash table with the new inner batch (which could be empty)
	 */
/*	ExecHashTableReset(hashtable);

	innerFile = hashtable->innerBatchFile[curbatch];

	if (innerFile != NULL)
	{
		if (BufFileSeek(innerFile, 0, 0L, SEEK_SET))
			ereport(ERROR,
					(errcode_for_file_access(),
				   errmsg("could not rewind hash-join temporary file: %m")));

		while ((slot = ExecHashJoinGetSavedTuple(hjstate,
												 innerFile,
												 &hashvalue,
												 hjstate->hj_InnerHashTupleSlot)))
		{*/
			/*
			 * NOTE: some tuples may be sent to future batches.  Also, it is
			 * possible for hashtable->nbatch to be increased here!
			 */
		/*	ExecHashTableInsert(hashtable,
								ExecFetchSlotTuple(slot),
								hashvalue);
		}
*/
		/*
		 * after we build the hash table, the inner batch file is no longer
		 * needed
		 */
/*		BufFileClose(innerFile);
		hashtable->innerBatchFile[curbatch] = NULL;
	}
*/
	/*
	 * If there's no outer batch file, advance to next batch.
	 */
/*	if (hashtable->outerBatchFile[curbatch] == NULL)
		goto start_over;
*/
	/*
	 * Rewind outer batch file, so that we can start reading it.
	 */
/*	if (BufFileSeek(hashtable->outerBatchFile[curbatch], 0, 0L, SEEK_SET))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not rewind hash-join temporary file: %m")));

	return curbatch;*/
	
	return 0;
}

/*
 * ExecHashJoinSaveTuple
 *		save a tuple to a batch file.
 *
 * The data recorded in the file for each tuple is its hash value,
 * then an image of its HeapTupleData (with meaningless t_data pointer)
 * followed by the HeapTupleHeader and tuple data.
 *
 * Note: it is important always to call this in the regular executor
 * context, not in a shorter-lived context; else the temp file buffers
 * will get messed up.
 */
void
ExecHashJoinSaveTuple(HeapTuple heapTuple, uint32 hashvalue,
					  BufFile **fileptr)
{
	BufFile    *file = *fileptr;
	size_t		written;

	if (file == NULL)
	{
		/* First write to this batch file, so open it. */
		file = BufFileCreateTemp(false);
		*fileptr = file;
	}

	written = BufFileWrite(file, (void *) &hashvalue, sizeof(uint32));
	if (written != sizeof(uint32))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to hash-join temporary file: %m")));

	written = BufFileWrite(file, (void *) heapTuple, sizeof(HeapTupleData));
	if (written != sizeof(HeapTupleData))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to hash-join temporary file: %m")));

	written = BufFileWrite(file, (void *) heapTuple->t_data, heapTuple->t_len);
	if (written != (size_t) heapTuple->t_len)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not write to hash-join temporary file: %m")));
}

/*
 * ExecHashJoinGetSavedTuple
 *		read the next tuple from a batch file.	Return NULL if no more.
 *
 * On success, *hashvalue is set to the tuple's hash value, and the tuple
 * itself is stored in the given slot.
 */
static TupleTableSlot *
ExecHashJoinGetSavedTuple(HashJoinState *hjstate,
						  BufFile *file,
						  uint32 *hashvalue,
						  TupleTableSlot *tupleSlot)
{
	HeapTupleData htup;
	size_t		nread;
	HeapTuple	heapTuple;

	nread = BufFileRead(file, (void *) hashvalue, sizeof(uint32));
	if (nread == 0)
		return NULL;			/* end of file */
	if (nread != sizeof(uint32))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read from hash-join temporary file: %m")));
	nread = BufFileRead(file, (void *) &htup, sizeof(HeapTupleData));
	if (nread != sizeof(HeapTupleData))
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read from hash-join temporary file: %m")));
	heapTuple = palloc(HEAPTUPLESIZE + htup.t_len);
	memcpy((char *) heapTuple, (char *) &htup, sizeof(HeapTupleData));
	heapTuple->t_datamcxt = CurrentMemoryContext;
	heapTuple->t_data = (HeapTupleHeader)
		((char *) heapTuple + HEAPTUPLESIZE);
	nread = BufFileRead(file, (void *) heapTuple->t_data, htup.t_len);
	if (nread != (size_t) htup.t_len)
		ereport(ERROR,
				(errcode_for_file_access(),
				 errmsg("could not read from hash-join temporary file: %m")));
	return ExecStoreTuple(heapTuple, tupleSlot, InvalidBuffer, true);
}


void
ExecReScanHashJoin(HashJoinState *node, ExprContext *exprCtxt)
{
	/*
	 * In a multi-batch join, we currently have to do rescans the hard way,
	 * primarily because batch temp files may have already been released. But
	 * if it's a single-batch join, and there is no parameter change for the
	 * inner subnode, then we can just re-use the existing hash table without
	 * rebuilding it.
	 */
	if (node->hj_InnerHashTable != NULL)
	{
		if (node->hj_InnerHashTable->nbatch == 1 &&
			((PlanState *) node)->righttree->chgParam == NULL)
		{
			/*
			 * okay to reuse the hash table; needn't rescan inner, either.
			 *
			 * What we do need to do is reset our state about the emptiness
			 * of the outer relation, so that the new scan of the outer will
			 * update it correctly if it turns out to be empty this time.
			 * (There's no harm in clearing it now because ExecHashJoin won't
			 * need the info.  In the other cases, where the hash table
			 * doesn't exist or we are destroying it, we leave this state
			 * alone because ExecHashJoin will need it the first time
			 * through.)
			 */
			//CSI3130
			/*node->hj_OuterNotEmpty = false; */
		}
		else
		{
			/* must destroy and rebuild hash table */
			ExecHashTableDestroy(node->hj_InnerHashTable);
			node->hj_InnerHashTable = NULL;

			/*
			 * if chgParam of subnode is not null then plan will be re-scanned
			 * by first ExecProcNode.
			 */
			if (((PlanState *) node)->righttree->chgParam == NULL)
				ExecReScan(((PlanState *) node)->righttree, exprCtxt);
		}
	}
	
	//CSI3130 Commented out original code
	/* Always reset intra-tuple state */
	/*node->hj_InnerCurHashValue = 0;
	node->hj_InnerCurBucketNo = 0;
	node->hj_InnerCurTuple = NULL;
	*/
	
	node->js.ps.ps_OuterTupleSlot = NULL;
	node->js.ps.ps_TupFromTlist = false;
	node->hj_NeedNewOuter = true;
	node->hj_MatchedOuter = false;
	node->hj_FirstOuterTupleSlot = NULL;

	/*
	 * if chgParam of subnode is not null then plan will be re-scanned by
	 * first ExecProcNode.
	 */
	if (((PlanState *) node)->lefttree->chgParam == NULL)
		ExecReScan(((PlanState *) node)->lefttree, exprCtxt);
}
