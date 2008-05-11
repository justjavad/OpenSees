/* ****************************************************************** **
**    OpenSees - Open System for Earthquake Engineering Simulation    **
**          Pacific Earthquake Engineering Research Center            **
**                                                                    **
**                                                                    **
** (C) Copyright 2001, The Regents of the University of California    **
** All Rights Reserved.                                               **
**                                                                    **
** Commercial use of this program without express permission of the   **
** University of California, Berkeley, is strictly prohibited.  See   **
** file 'COPYRIGHT'  in main directory for information on usage and   **
** redistribution,  and for a DISCLAIMER OF ALL WARRANTIES.           **
**                                                                    **
** Developed by:                                                      **
**   Frank McKenna (fmckenna@ce.berkeley.edu)                         **
**   Gregory L. Fenves (fenves@ce.berkeley.edu)                       **
**   Filip C. Filippou (filippou@ce.berkeley.edu)                     **
**                                                                    **
** Reliability module developed by:                                   **
**   Terje Haukaas (haukaas@ce.berkeley.edu)                          **
**   Armen Der Kiureghian (adk@ce.berkeley.edu)                       **
**                                                                    **
** ****************************************************************** */
                                                                        
// $Revision: 1.11 $
// $Date: 2008-05-11 19:52:54 $
// $Source: /usr/local/cvs/OpenSees/SRC/reliability/analysis/sensitivity/FiniteDifferenceGradGEvaluator.cpp,v $


//
// Written by Terje Haukaas (haukaas@ce.berkeley.edu)
//

#include <FiniteDifferenceGradGEvaluator.h>
#include <Vector.h>
#include <Matrix.h>
#include <GradGEvaluator.h>
#include <ReliabilityDomain.h>
#include <LimitStateFunction.h>
#include <GFunEvaluator.h>
#include <RandomVariable.h>
#include <tcl.h>

#include <RandomVariableIter.h>
#include <LimitStateFunctionIter.h>

#include <fstream>
#include <iomanip>
#include <iostream>
using std::ifstream;
using std::ios;
using std::setw;
using std::setprecision;
using std::setiosflags;


FiniteDifferenceGradGEvaluator::FiniteDifferenceGradGEvaluator(
					GFunEvaluator *passedGFunEvaluator,
					ReliabilityDomain *passedReliabilityDomain,
					Tcl_Interp *passedTclInterp,
					double passedPerturbationFactor,
					bool PdoGradientCheck,
					bool pReComputeG)
:GradGEvaluator(passedReliabilityDomain, passedTclInterp)
{
	theGFunEvaluator = passedGFunEvaluator;
	perturbationFactor = passedPerturbationFactor;
	doGradientCheck = PdoGradientCheck;
	reComputeG = pReComputeG;

	int nrv = passedReliabilityDomain->getNumberOfRandomVariables();
	int lsf = passedReliabilityDomain->getNumberOfLimitStateFunctions();
	grad_g = new Vector(nrv);
	grad_g_matrix = new Matrix(nrv,lsf);

	DgDdispl = 0;
	
}

FiniteDifferenceGradGEvaluator::~FiniteDifferenceGradGEvaluator()
{
	delete grad_g;
	delete grad_g_matrix;
	
	if (DgDdispl != 0)
		delete DgDdispl;

}



Vector
FiniteDifferenceGradGEvaluator::getGradG()
{
	return (*grad_g);
}



Matrix
FiniteDifferenceGradGEvaluator::getAllGradG()
{
	return (*grad_g_matrix);
}


int
FiniteDifferenceGradGEvaluator::computeGradG(double gFunValue,
					     const Vector &passed_x)
{
	
	numberOfEvalIncSens++;///// added by K Fujimura /////

	// Call base class method
	computeParameterDerivatives(gFunValue);


	// Possibly re-compute limit-state function value
	int result;
	if (reComputeG) {
		result = theGFunEvaluator->runGFunAnalysis(passed_x);
		if (result < 0) {
			opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
				<< " could not run analysis to evaluate limit-state function. " << endln;
			return -1;
		}
		result = theGFunEvaluator->evaluateG(passed_x);
		if (result < 0) {
			opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
				<< " could not tokenize limit-state function. " << endln;
			return -1;
		}
		gFunValue = theGFunEvaluator->getG();
	}


	// Initial declarations
	int numberOfRandomVariables = passed_x.Size();
	Vector perturbed_x(numberOfRandomVariables);
	RandomVariable *theRandomVariable;
	double h;
	double gFunValueAStepAhead;
	double stdv;

	RandomVariableIter rvIter = theReliabilityDomain->getRandomVariables();
	// For each random variable: perturb and run analysis again
	//for ( i=0 ; i<numberOfRandomVariables ; i++ ) {
	while ((theRandomVariable = rvIter()) != 0) {
	  
	  int rvTag = theRandomVariable->getTag();
	  //int i = theRandomVariable->getIndex();
	  int i = theReliabilityDomain->getRandomVariableIndex(rvTag);

	  //opserr << "tag: " << rvTag << ", index: " << i << endln;

		// Get the standard deviation
		stdv = theRandomVariable->getStdv();


		// Compute perturbation
		h = stdv/perturbationFactor;


		// Compute perturbed vector of random variables realization
		perturbed_x = passed_x;
		perturbed_x(i) = perturbed_x(i) + h;


		// Evaluate limit-state function
		result = theGFunEvaluator->runGFunAnalysis(perturbed_x);
		if (result < 0) {
			opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
				<< " could not run analysis to evaluate limit-state function. " << endln;
			return -1;
		}
		result = theGFunEvaluator->evaluateG(perturbed_x);
		if (result < 0) {
		  opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
			 << " could not tokenize limit-state function. " << endln;
		  return -1;
		}
		gFunValueAStepAhead = theGFunEvaluator->getG();


		// Compute the derivative by finite difference
		(*grad_g)(i) = (gFunValueAStepAhead - gFunValue) / h;
	}


	if (doGradientCheck) {
		char myString[100];
		ofstream outputFile( "FFDgradients.out", ios::out );
		opserr << endln;
		for (int ffd=0; ffd<grad_g->Size(); ffd++) {
			opserr << "FFD("<< (ffd+1) << ") = " << (*grad_g)(ffd) << endln;
			sprintf(myString,"%20.16e ",(*grad_g)(ffd));
			outputFile << myString << endln;
		}
		outputFile.close();
		opserr << "PRESS Ctrl+C TO TERMINATE APPLICATION!" << endln;
		// should never have an infinite loop in a program........
		while(true) {
		}
	}

	return 0;
}




int
FiniteDifferenceGradGEvaluator::computeAllGradG(const Vector &gFunValues,
						const Vector &passed_x)
{

	numberOfEvalIncSens++;///// added by K Fujimura /////

	// Get number of random variables
	int nrv = theReliabilityDomain->getNumberOfRandomVariables();

	// Zero result matrix
	grad_g_matrix->Zero();

	// Initial declarations
	Vector perturbed_x(nrv);
	RandomVariable *theRandomVariable;
	double h;
	double gFunValueAStepAhead;
	double stdv;
	int result;


	// For each random variable: perturb and run analysis again
	RandomVariableIter rvIter = theReliabilityDomain->getRandomVariables();
	// For each random variable: perturb and run analysis again
	//for (int i=1; i<=nrv; i++) {
	while ((theRandomVariable = rvIter()) != 0) {

	  int rvTag = theRandomVariable->getTag();
	  //int i = theRandomVariable->getIndex();
	  int i = theReliabilityDomain->getRandomVariableIndex(rvTag);

		// Get the standard deviation
		stdv = theRandomVariable->getStdv();


		// Compute perturbation
		h = stdv/perturbationFactor;


		// Compute perturbed vector of random variables realization
		perturbed_x = passed_x;
		perturbed_x(i) = perturbed_x(i) + h;


		// Evaluate limit-state function
		result = theGFunEvaluator->runGFunAnalysis(perturbed_x);
		if (result < 0) {
			opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
				<< " could not run analysis to evaluate limit-state function. " << endln;
			return -1;
		}

		LimitStateFunctionIter lsfIter = theReliabilityDomain->getLimitStateFunctions();
		LimitStateFunction *theLSF;
		// Now loop over limit-state functions
		//for (int j=1; j<=lsf; j++) {
		while ((theLSF = lsfIter()) != 0) {
		  
		  int lsfTag = theLSF->getTag();
		  int j = theReliabilityDomain->getLimitStateFunctionIndex(lsfTag);

			// Set tag of active limit-state function
			theReliabilityDomain->setTagOfActiveLimitStateFunction(lsfTag);

			// Limit-state function value
			result = theGFunEvaluator->evaluateG(perturbed_x);
			if (result < 0) {
				opserr << "FiniteDifferenceGradGEvaluator::evaluate_grad_g() - " << endln
					<< " could not tokenize limit-state function. " << endln;
				return -1;
			}
			gFunValueAStepAhead = theGFunEvaluator->getG();

			// Compute the derivative by finite difference
			(*grad_g_matrix)(i,j) = (gFunValueAStepAhead - gFunValues(j)) / h;

		}
	}

	return 0;
}





Matrix
FiniteDifferenceGradGEvaluator::getDgDdispl()
{
	// This method is implemented solely for the purpose of mean 
	// out-crossing rate analysis using "two searches". Then the 
	// derivative of the limit-state function wrt. the displacement
	// is needed to expand the limit-state funciton expression

	// Result matrix
	Matrix *DgDdispl = 0;


	// Initial declaractions
	double perturbationFactor = 0.001; // (is multiplied by stdv and added to others...)
	char tclAssignment[500];
	char *dollarSign = "$";
	char *underscore = "_";
	char lsf_expression[500] = "";
	char separators[5] = "}{";
	char tempchar[100]="";
	char newSeparators[5] = "_";
	double g, g_perturbed;
	int i;

	// "Download" limit-state function from reliability domain
	int lsf = theReliabilityDomain->getTagOfActiveLimitStateFunction();
	LimitStateFunction *theLimitStateFunction = 
		theReliabilityDomain->getLimitStateFunctionPtr(lsf);
	char *theExpression = theLimitStateFunction->getExpression();
	char lsf_copy[500];
	strcpy(lsf_copy,theExpression);


	// Tokenize the limit-state function and COMPUTE GRADIENTS
	char *tokenPtr = strtok( lsf_copy, separators); 
	while ( tokenPtr != NULL ) {

		strcpy(tempchar,tokenPtr);

		// If a nodal displacement is detected
		if ( strncmp(tokenPtr, "u", 1) == 0) {

			// Get node number and dof number
			int nodeNumber, direction;
			sscanf(tempchar,"u_%i_%i", &nodeNumber, &direction);

			// Evaluate the limit-state function again
			char *theTokenizedExpression = theLimitStateFunction->getTokenizedExpression();
			if (Tcl_ExprDouble( theTclInterp, theTokenizedExpression, &g ) == TCL_ERROR) {
			  opserr << "ERROR FiniteDifferenceGradGEvaluator -- Tcl_ExprDouble returned error" << endln;
			  //return -1;
			}
			
			// Keep the original displacement value
			double originalValue;
			sprintf(tclAssignment,"$u_%d_%d", nodeNumber, direction);
			if (Tcl_ExprDouble( theTclInterp, tclAssignment, &originalValue) == TCL_ERROR) {
			  opserr << "ERROR FiniteDifferenceGradGEvaluator -- Tcl_ExprDouble returned error" << endln;
			  //return -1;
			}

			// Set perturbed value in the Tcl workspace
			double newValue = originalValue*(1.0+perturbationFactor);
			sprintf(tclAssignment,"set u_%d_%d %35.20f", nodeNumber, direction, newValue);
			if (Tcl_Eval( theTclInterp, tclAssignment) == TCL_ERROR) {
			 opserr << "ERROR FiniteDifferenceGradGEvaluator -- Tcl_Eval returned error" << endln;
			 //return -1; 
			}

			// Evaluate the limit-state function again
			if (Tcl_ExprDouble( theTclInterp, theTokenizedExpression, &g_perturbed ) == TCL_ERROR) {
			  opserr << "ERROR FiniteDifferenceGradGEvaluator -- Tcl_ExprDouble returned error" << endln;
			  //return -1;
			}

			// Compute gradient
			double onedgdu = (g_perturbed-g)/(originalValue*perturbationFactor);

			// Store the DgDdispl in a matrix
			if (DgDdispl == 0) {
				DgDdispl = new Matrix(1, 3);
				(*DgDdispl)(0,0) = (double)nodeNumber;
				(*DgDdispl)(0,1) = (double)direction;
				(*DgDdispl)(0,2) = onedgdu;
			}
			else {
				int oldSize = DgDdispl->noRows();
				Matrix tempMatrix = *DgDdispl;
				delete DgDdispl;
				DgDdispl = new Matrix(oldSize+1, 3);
				for (i=0; i<oldSize; i++) {
					(*DgDdispl)(i,0) = tempMatrix(i,0);
					(*DgDdispl)(i,1) = tempMatrix(i,1);
					(*DgDdispl)(i,2) = tempMatrix(i,2);
				}
				(*DgDdispl)(oldSize,0) = (double)nodeNumber;
				(*DgDdispl)(oldSize,1) = (double)direction;
				(*DgDdispl)(oldSize,2) = onedgdu;
			}


			// Make assignment back to its original value
			sprintf(tclAssignment,"set u_%d_%d %35.20f", nodeNumber, direction, originalValue);
			if (Tcl_Eval( theTclInterp, tclAssignment) == TCL_ERROR) {
			 opserr << "ERROR FiniteDifferenceGradGEvaluator -- Tcl_Eval returned error" << endln;
			 //return -1; 
			}

		}

		tokenPtr = strtok( NULL, separators);  // read next token and go up and check the while condition again
	} 

	return (*DgDdispl);
}

