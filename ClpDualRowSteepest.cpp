// Copyright (C) 2002, International Business Machines
// Corporation and others.  All Rights Reserved.
#if defined(_MSC_VER)
// Turn off compiler warning about long names
#  pragma warning(disable:4786)
#endif


#include "ClpSimplex.hpp"
#include "ClpDualRowSteepest.hpp"
#include "OsiIndexedVector.hpp"
#include "ClpFactorization.hpp"
#include "CoinHelperFunctions.hpp"
#include <stdio.h>

//#############################################################################
// Constructors / Destructor / Assignment
//#############################################################################

//-------------------------------------------------------------------
// Default Constructor 
//-------------------------------------------------------------------
ClpDualRowSteepest::ClpDualRowSteepest (int mode) 
  : ClpDualRowPivot(),
    state_(-1),
    mode_(mode),
    weights_(NULL),
    infeasible_(NULL),
    alternateWeights_(NULL),
    savedWeights_(NULL)
{
  type_=2;
}

//-------------------------------------------------------------------
// Copy constructor 
//-------------------------------------------------------------------
ClpDualRowSteepest::ClpDualRowSteepest (const ClpDualRowSteepest & rhs) 
: ClpDualRowPivot(rhs)
{  
  state_=rhs.state_;
  mode_ = rhs.mode_;
  model_ = rhs.model_;
  if (rhs.infeasible_) {
    infeasible_= new OsiIndexedVector(rhs.infeasible_);
  } else {
    infeasible_=NULL;
  }
  if (rhs.weights_) {
    assert(model_);
    int number = model_->numberRows();
    weights_= new double[number];
    CoinDisjointCopyN(rhs.weights_,number,weights_);
  } else {
    weights_=NULL;
  }
  if (rhs.alternateWeights_) {
    alternateWeights_= new OsiIndexedVector(rhs.alternateWeights_);
  } else {
    alternateWeights_=NULL;
  }
  if (rhs.savedWeights_) {
    savedWeights_= new OsiIndexedVector(rhs.savedWeights_);
  } else {
    savedWeights_=NULL;
  }
}

//-------------------------------------------------------------------
// Destructor 
//-------------------------------------------------------------------
ClpDualRowSteepest::~ClpDualRowSteepest ()
{
  delete [] weights_;
  delete infeasible_;
  delete alternateWeights_;
  delete savedWeights_;
}

//----------------------------------------------------------------
// Assignment operator 
//-------------------------------------------------------------------
ClpDualRowSteepest &
ClpDualRowSteepest::operator=(const ClpDualRowSteepest& rhs)
{
  if (this != &rhs) {
    ClpDualRowPivot::operator=(rhs);
    state_=rhs.state_;
    mode_ = rhs.mode_;
    model_ = rhs.model_;
    delete [] weights_;
    delete infeasible_;
    delete alternateWeights_;
    delete savedWeights_;
    if (rhs.infeasible_!=NULL) {
      infeasible_= new OsiIndexedVector(rhs.infeasible_);
    } else {
      infeasible_=NULL;
    }
    if (rhs.weights_!=NULL) {
      assert(model_);
      int number = model_->numberRows();
      weights_= new double[number];
      CoinDisjointCopyN(rhs.weights_,number,weights_);
    } else {
      weights_=NULL;
    }
    if (rhs.alternateWeights_!=NULL) {
      alternateWeights_= new OsiIndexedVector(rhs.alternateWeights_);
    } else {
      alternateWeights_=NULL;
    }
    if (rhs.savedWeights_!=NULL) {
      savedWeights_= new OsiIndexedVector(rhs.savedWeights_);
    } else {
      savedWeights_=NULL;
    }
  }
  return *this;
}

// Returns pivot row, -1 if none
int 
ClpDualRowSteepest::pivotRow()
{
  assert(model_);
  int i,iRow;
  double * infeas = infeasible_->denseVector();
  double largest=1.0e-20;
  int * index = infeasible_->getIndices();
  int number = infeasible_->getNumElements();
  const int * pivotVariable =model_->pivotVariable();
  int chosenRow=-1;
  double tolerance=model_->currentPrimalTolerance();
  // we can't really trust infeasibilities if there is primal error
  if (model_->largestPrimalError()>1.0e-8)
    tolerance *= model_->largestPrimalError()/1.0e-8;
  tolerance *= tolerance; // as we are using squares
  for (i=0;i<number;i++) {
    iRow = index[i];
    double value = infeas[iRow];
    if (value>largest*weights_[iRow]&&value>tolerance) {
      int iSequence = pivotVariable[iRow];
      if (!model_->flagged(iSequence)) {
	chosenRow=iRow;
	largest=value/weights_[iRow];
      }
    }
  }
  return chosenRow;
}
#define TRY_NORM 1.0e-4
// Updates weights 
void 
ClpDualRowSteepest::updateWeights(OsiIndexedVector * input,
				  OsiIndexedVector * spare,
				  OsiIndexedVector * updatedColumn)
{
  // clear other region
  alternateWeights_->clear();
  double norm = 0.0;
  double * work = input->denseVector();
  int number = input->getNumElements();
  int * which = input->getIndices();
  double * work2 = spare->denseVector();
  int * which2 = spare->getIndices();
  double * work3 = alternateWeights_->denseVector();
  int * which3 = alternateWeights_->getIndices();
  int i;
#if CLP_DEBUG>2
  // Very expensive debug
  {
    int numberRows = model_->numberRows();
    OsiIndexedVector * temp = new OsiIndexedVector();
    temp->reserve(numberRows+
		  model_->factorization()->maximumPivots());
    double * array = alternateWeights_->denseVector();
    int * which = alternateWeights_->getIndices();
    for (i=0;i<numberRows;i++) {
      double value=0.0;
      array[i] = 1.0;
      which[0] = i;
      alternateWeights_->setNumElements(1);
      model_->factorization()->updateColumnTranspose(temp,
						     alternateWeights_);
      int number = alternateWeights_->getNumElements();
      int j;
      for (j=0;j<number;j++) {
	int iRow=which[j];
	value+=array[iRow]*array[iRow];
	array[iRow]=0.0;
      }
      alternateWeights_->setNumElements(0);
      if (fabs(weights_[i]-value)>1.0e-4)
	printf("%d old %g, true %g\n",i,weights_[i],value);
    }
    delete temp;
  }
#endif
  for (i=0;i<number;i++) {
    int iRow = which[i];
    double value = work[iRow];
    norm += value*value;
    work2[iRow]=value;
    which2[i]=iRow;
  }
  spare->setNumElements(number);
  // ftran
  model_->factorization()->updateColumn(alternateWeights_,spare);
  // alternateWeights_ should still be empty
  int pivotRow = model_->pivotRow();
#ifdef CLP_DEBUG
  if ( model_->logLevel (  ) >4  && 
       fabs(norm-weights_[pivotRow])>1.0e-3*(1.0+norm)) 
    printf("on row %d, true weight %g, old %g\n",
	   pivotRow,sqrt(norm),sqrt(weights_[pivotRow]));
#endif
  // could re-initialize here (could be expensive)
  norm /= model_->alpha() * model_->alpha();

  assert(norm);
  if (norm < TRY_NORM) 
    norm = TRY_NORM;
  if (norm != 0.) {
    double multiplier = 2.0 / model_->alpha();
    // look at updated column
    work = updatedColumn->denseVector();
    number = updatedColumn->getNumElements();
    which = updatedColumn->getIndices();

    int nSave=0;

    for (i =0; i < number; i++) {
      int iRow = which[i];
      double theta = work[iRow];
      if (theta) {
	double devex = weights_[iRow];
	work3[iRow]=devex; // save old
	which3[nSave++]=iRow;
	double value = work2[iRow];
	devex +=  theta * (theta*norm+value * multiplier);
	if (devex < TRY_NORM) 
	  devex = TRY_NORM;
	weights_[iRow]=devex;
      }
    }
    assert(work3[pivotRow]&&work[pivotRow]);
    alternateWeights_->setNumElements(nSave);
    if (norm < TRY_NORM) 
      norm = TRY_NORM;
    weights_[pivotRow] = norm;
  }
  spare->clear();
#ifdef CLP_DEBUG
  spare->checkClear();
#endif
}
  
/* Updates primal solution (and maybe list of candidates)
   Uses input vector which it deletes
   Computes change in objective function
*/
void 
ClpDualRowSteepest::updatePrimalSolution(
					OsiIndexedVector * primalUpdate,
					double primalRatio,
					double & objectiveChange)
{
  double * work = primalUpdate->denseVector();
  int number = primalUpdate->getNumElements();
  int * which = primalUpdate->getIndices();
  int i;
  double changeObj=0.0;
  double tolerance=model_->currentPrimalTolerance();
  const int * pivotVariable = model_->pivotVariable();
  double * infeas = infeasible_->denseVector();
  for (i=0;i<number;i++) {
    int iRow=which[i];
    int iPivot=pivotVariable[iRow];
    double & value = model_->solutionAddress(iPivot);
    double cost = model_->cost(iPivot);
    double change = primalRatio*work[iRow];
    value -= change;
    double lower = model_->lower(iPivot);
    double upper = model_->upper(iPivot);
    if (value>upper+tolerance) {
      // store square in list
      if (infeas[iRow])
	infeas[iRow] = (value-upper)*(value-upper); // already there
      else
	infeasible_->quickAdd(iRow,(value-upper)*(value-upper));
    } else if (value<lower-tolerance) {
      // store square in list
      if (infeas[iRow])
	infeas[iRow] = (value-lower)*(value-lower); // already there
      else
	infeasible_->quickAdd(iRow,(value-lower)*(value-lower));
    } else {
      // feasible - was it infeasible - if so set tiny
      if (infeas[iRow])
	infeas[iRow] = 1.0e-100;
    }
    changeObj -= change*cost;
    work[iRow]=0.0;
  }
  infeasible_->stopQuickAdd();
  primalUpdate->setNumElements(0);
  objectiveChange += changeObj;
}
/* Saves any weights round factorization as pivot rows may change
   1) before factorization
   2) after factorization
   3) just redo infeasibilities
   4) restore weights
*/
void 
ClpDualRowSteepest::saveWeights(ClpSimplex * model,int mode)
{
  // alternateWeights_ is defined as indexed but is treated oddly
  model_ = model;
  int numberRows = model_->numberRows();
  int numberColumns = model_->numberColumns();
  const int * pivotVariable = model_->pivotVariable();
  int i;
  if (mode==1&&weights_) {
    alternateWeights_->clear();
    // change from row numbers to sequence numbers
    int * which = alternateWeights_->getIndices();
    for (i=0;i<numberRows;i++) {
      int iPivot=pivotVariable[i];
      which[i]=iPivot;
    }
    state_=1;
  } else if (mode==2||mode==4) {
    // restore
    if (!weights_||state_==-1) {
      // initialize weights
      delete [] weights_;
      delete alternateWeights_;
      weights_ = new double[numberRows];
      alternateWeights_ = new OsiIndexedVector();
      // enough space so can use it for factorization
      alternateWeights_->reserve(numberRows+
				 model_->factorization()->maximumPivots());
      if (!mode_) {
	// initialize to 1.0 (can we do better?)
	for (i=0;i<numberRows;i++) {
	  weights_[i]=1.0;
	}
      } else {
	OsiIndexedVector * temp = new OsiIndexedVector();
	temp->reserve(numberRows+
		      model_->factorization()->maximumPivots());
	double * array = alternateWeights_->denseVector();
	int * which = alternateWeights_->getIndices();
	for (i=0;i<numberRows;i++) {
	  double value=0.0;
	  array[i] = 1.0;
	  which[0] = i;
	  alternateWeights_->setNumElements(1);
	  model_->factorization()->updateColumnTranspose(temp,
							 alternateWeights_);
	  int number = alternateWeights_->getNumElements();
	  int j;
	  for (j=0;j<number;j++) {
	    int iRow=which[j];
	    value+=array[iRow]*array[iRow];
	    array[iRow]=0.0;
	  }
	  alternateWeights_->setNumElements(0);
	  weights_[i] = value;
	}
	delete temp;
      }
      // create saved weights (not really indexedvector)
      savedWeights_ = new OsiIndexedVector();
      savedWeights_->reserve(numberRows);
      
      double * array = savedWeights_->denseVector();
      int * which = savedWeights_->getIndices();
      for (i=0;i<numberRows;i++) {
	array[i]=weights_[i];
	which[i]=pivotVariable[i];
      }
    } else {
      int * which = alternateWeights_->getIndices();
      if (mode!=4) {
	// save
	memcpy(savedWeights_->getIndices(),which,
	       numberRows*sizeof(int));
	memcpy(savedWeights_->denseVector(),weights_,
	       numberRows*sizeof(double));
      } else {
	// restore
	memcpy(which,savedWeights_->getIndices(),
	       numberRows*sizeof(int));
	memcpy(weights_,savedWeights_->denseVector(),
	       numberRows*sizeof(double));
      }
      // restore (a bit slow - but only every re-factorization)
      double * array = new double[numberRows+numberColumns];
      for (i=0;i<numberRows;i++) {
	int iSeq=which[i];
	array[iSeq]=weights_[i];
      }
      for (i=0;i<numberRows;i++) {
	int iPivot=pivotVariable[i];
	weights_[i]=array[iPivot];
	if (weights_[i]<TRY_NORM)
	  weights_[i] = TRY_NORM; // may need to check more
      }
      delete [] array;
    }
    state_=0;
    // set up infeasibilities
    if (!infeasible_) {
      infeasible_ = new OsiIndexedVector();
      infeasible_->reserve(numberRows);
    }
  }
  if (mode>=2) {
    infeasible_->clear();
    int iRow;
    const int * pivotVariable = model_->pivotVariable();
    double tolerance=model_->currentPrimalTolerance();
    for (iRow=0;iRow<model_->numberRows();iRow++) {
      int iPivot=pivotVariable[iRow];
      double value = model_->solution(iPivot);
      double lower = model_->lower(iPivot);
      double upper = model_->upper(iPivot);
      if (value>upper+tolerance) {
	value -= upper;
	// store square in list
	infeasible_->quickAdd(iRow,value*value);
      } else if (value<lower-tolerance) {
	value -= lower;
	// store square in list
	infeasible_->quickAdd(iRow,value*value);
      }
    }
    infeasible_->stopQuickAdd();
  }
}
// Gets rid of last update
void 
ClpDualRowSteepest::unrollWeights()
{
  double * saved = alternateWeights_->denseVector();
  int number = alternateWeights_->getNumElements();
  int * which = alternateWeights_->getIndices();
  int i;
  for (i=0;i<number;i++) {
    int iRow = which[i];
    weights_[iRow]=saved[iRow];
    saved[iRow]=0.0;
  }
  alternateWeights_->setNumElements(0);
}
//-------------------------------------------------------------------
// Clone
//-------------------------------------------------------------------
ClpDualRowPivot * ClpDualRowSteepest::clone(bool CopyData) const
{
  if (CopyData) {
    return new ClpDualRowSteepest(*this);
  } else {
    return new ClpDualRowSteepest();
  }
}

