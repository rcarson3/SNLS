#include <cstdlib>
#include <iostream>

#include <gtest/gtest.h>

using namespace std;

#include "SNLS_TrDLDenseG.h"

#ifndef LAMBDA_BROYDEN 
#define LAMBDA_BROYDEN 0.9999
#endif

#ifndef DEBUG
#define DEBUG 0
#endif

#define NL_MAXITER 200
#define NL_TOLER 1e-12

/*!
  Comment as in the Trilinos NOX package:
  
  This test problem is a modified extension of the "Broyden
  Tridiagonal Problem" from Jorge J. More', Burton S. Garbow, and
  Kenneth E. Hillstrom, Testing Unconstrained Optimization Software,
  ACM TOMS, Vol. 7, No. 1, March 1981, pp. 14-41.  The modification
  involves squaring the last equation fn(x) and using it in a
  homotopy-type equation.

  The parameter "lambda" is a homotopy-type parameter that may be
  varied from 0 to 1 to adjust the ill-conditioning of the problem.
  A value of 0 is the original, unmodified problem, while a value of
  1 is that problem with the last equation squared.  Typical values
  for increasingly ill-conditioned problems might be 0.9, 0.99,
  0.999, etc.

  The standard starting point is x(i) = -1, but setting x(i) = 0 tests
  the selected global strategy.
*/
class Broyden
{
public:
   static const int nDimSys = 8 ;

   // constructor
   __snls_hdev__  Broyden(double lambda )
      : _lambda(-1)
      {
         _lambda = lambda ;
         std::cout << "Broyden ill-conditioning: lambda = "
                   << std::setw(21) << std::setprecision(11) << _lambda << std::endl;          
      } ;

   __snls_hdev__ bool computeRJ(double* const r,
                                double* const J,
                                const double* const x )
      {
         double fn ;
         const int nDim = nDimSys ; // convenience -- less code change below
         
#if DEBUG > 1
         std::cout << "Evaluating at x = " ;
         for (int i=1; i<nDim; ++i) {
            std::cout << std::setw(21) << std::setprecision(11) << x[i] << " ";
         }
         std::cout << std::endl ;
#endif

         bool doComputeJ = (J != nullptr) ;
         if ( doComputeJ ) {
            for ( int ijJ=0; ijJ<_nXn; ++ijJ ) {
               J[ijJ] = 0.0 ;
            }
         }
         
         r[0] = (3-2*x[0])*x[0] - 2*x[1] + 1;
         for (int i=1; i<nDim-1; i++)
            r[i] = (3-2*x[i])*x[i] - x[i-1] - 2*x[i+1] + 1;

         fn = (3-2*x[nDim-1])*x[nDim-1] - x[nDim-2] + 1;
         r[nDim-1] = (1-_lambda)*fn + _lambda*(fn*fn);

         if ( doComputeJ ) {
            // F(0) = (3-2*x[0])*x[0] - 2*x[1] + 1;
            J[SNLSTRDLDG_J_INDX(0,0,nDim)] = 3 - 4*x[0];
            J[SNLSTRDLDG_J_INDX(0,1,nDim)] = -2;

            // F(i) = (3-2*x[i])*x[i] - x[i-1] - 2*x[i+1] + 1;
            for (int i=1; i<nDim-1; i++) {
               J[SNLSTRDLDG_J_INDX(i,i-1,nDim)] = -1;
               J[SNLSTRDLDG_J_INDX(i,i,nDim)]   = 3 - 4*x[i];
               J[SNLSTRDLDG_J_INDX(i,i+1,nDim)] = -2;
            }

            // F(n-1) = ((3-2*x[n-1])*x[n-1] - x[n-2] + 1)^2;
            fn = (3-2*x[nDim-1])*x[nDim-1] - x[nDim-2] + 1;
            double dfndxn = 3-4*x[nDim-1];
            J[SNLSTRDLDG_J_INDX(nDim-1,nDim-1,nDim)] = (1-_lambda)*(dfndxn) + _lambda*(2*dfndxn*fn);
            J[SNLSTRDLDG_J_INDX(nDim-1,nDim-2,nDim)] = (1-_lambda)*(-1) + _lambda*(-2*fn);
         }

         return true ;
         
      } ;
   
   private:
      double _lambda ;
      static const int _nXn = nDimSys*nDimSys ;
};

#ifndef __cuda_host_only__

__snls_device__
void Test_SNLSBroyden_D (Broyden *broyden)
{
   const int nDim = Broyden::nDimSys ;
   
   *broyden = Broyden( LAMBDA_BROYDEN ); 
   snls::SNLSTrDlDenseG<Broyden> solver(broyden) ;
   snls::TrDeltaControl deltaControlBroyden ;
   deltaControlBroyden._deltaInit = 1e0 ;
   solver.setupSolver(NL_MAXITER, NL_TOLER, &deltaControlBroyden);

   int    i   = (blockIdx.x * blockDim.x) + threadIdx.x; 

   printf("(%s::%s(ln=%d) cuda bx=%d bw=%2d thrd=%2d i=%2d broyden=%p)\n", __FILE__, __func__, __LINE__, blockIdx .x, blockDim .x, threadIdx.x, i, &broyden);

   // double r[nDim], J[nDim*nDim] ;
   double* x = solver.getXPntr() ;
   //
   for (int iX = 0; iX < nDim; ++iX) {
      x[iX] = 0e0 ;
   }

   solver.solve( ) ;
   
}

__snls_global__
void Test_SNLSBroyden_K
(
   const int   n         ///< max number of active threads
)
{
   int i = blockDim.x * blockIdx.x + threadIdx.x; 

   Broyden   broyden[40];

   if (i<n) { Test_SNLSBroyden_D(broyden+i); }

}

void snls::Test_SNLSBroyden_GPU(const int npoints)
{
   int nthrds  = 32;
   int nblks   = (npoints+(nthrds-1))/nthrds;

   Test_SNLSBroyden_K<<<nblks,nthrds>>>(npoints);
   cudaDeviceSynchronize();

   cudaError_t  cu_err     = cudaGetLastError();
   char        *cu_err_str = (char *) ( cu_err ? cudaGetErrorString(cu_err) : 0 );

   if (cu_err_str) { printf("%s::%s(ln=%d) : CUDA error=\"%s\"\n", __FILE__, __func__, __LINE__, cu_err_str ); exit(0); }
}

#endif
// ifndef __cuda_host_only__

TEST(snls,broyden_a) // int main(int , char ** )
{
   const int nDim = Broyden::nDimSys ;

   Broyden broyden( 0.9999 ) ; // LAMBDA_BROYDEN 
   snls::SNLSTrDlDenseG<Broyden> solver(broyden) ;
   snls::TrDeltaControl deltaControlBroyden ;
   deltaControlBroyden._deltaInit = 1e0 ;
   solver.setupSolver(NL_MAXITER, NL_TOLER, &deltaControlBroyden, 1);

   double* x = solver.getXPntr() ;
   for (int iX = 0; iX < nDim; ++iX) {
      x[iX] = 0e0 ;
   }
   //
   // double r[nDim], J[nDim*nDim] ;
   double* r = solver.getRPntr() ;
   double* J = solver.getJPntr() ;
   //
   solver._crj.computeRJ(r, J, x); // broyden.computeRJ(r, J, x);

   snls::SNLSStatus_t status = solver.solve( ) ;
   EXPECT_TRUE( status == snls::converged ) << "Expected solver to converge" ;
   if ( status != snls::converged ){
      char errmsg[256];
      snprintf(errmsg, sizeof(errmsg), "Solver failed to converge! Using tol=%g and maxIter=%i",NL_TOLER,NL_MAXITER);
      SNLS_FAIL(__func__,errmsg);
   }
   std::cout << "Function evaluations: " << solver.getNFEvals() << "\n";
   EXPECT_EQ( solver.getNFEvals(), 19 ) << "Expected 19 function evaluations for this case" ;
}

TEST(snls,broyden_b)
{
   const int nDim = Broyden::nDimSys ;

   Broyden broyden( 0.99999999 ) ; // LAMBDA_BROYDEN
   snls::SNLSTrDlDenseG<Broyden> solver(broyden) ;
   snls::TrDeltaControl deltaControlBroyden ;
   deltaControlBroyden._deltaInit = 100e0 ;
   solver.setupSolver(NL_MAXITER, NL_TOLER, &deltaControlBroyden, 1);

   double* x = solver.getXPntr() ;
   for (int iX = 0; iX < nDim; ++iX) {
      x[iX] = 0e0 ;
   }
   //
   // double r[nDim], J[nDim*nDim] ;
   double* r = solver.getRPntr() ;
   double* J = solver.getJPntr() ;
   //
   solver._crj.computeRJ(r, J, x); // broyden.computeRJ(r, J, x);

   snls::SNLSStatus_t status = solver.solve( ) ;
   EXPECT_TRUE( status == snls::converged ) << "Expected solver to converge" ;
   if ( status != snls::converged ){
      char errmsg[256];
      snprintf(errmsg, sizeof(errmsg), "Solver failed to converge! Using tol=%g and maxIter=%i",NL_TOLER,NL_MAXITER);
      SNLS_FAIL(__func__,errmsg);
   }
   std::cout << "Function evaluations: " << solver.getNFEvals() << "\n";    
   EXPECT_EQ( solver.getNFEvals(), 23 ) << "Expected 23 function evaluations for this case" ;
}


#ifdef __CUDACC__
TEST(snls,broyden_gpu_a)
{
   const int nDim = Broyden::nDimSys ;

   Broyden broyden( LAMBDA_BROYDEN ) ;
   snls::SNLSTrDlDenseG<Broyden> solver(broyden) ;
   snls::TrDeltaControl deltaControlBroyden ;
   deltaControlBroyden._deltaInit = 100e0 ;
   solver.setupSolver(NL_MAXITER, NL_TOLER, &deltaControlBroyden, 1);

   double* x = solver.getXPntr() ;
   for (int iX = 0; iX < nDim; ++iX) {
      x[iX] = 0e0 ;
   }
   //
   // double r[nDim], J[nDim*nDim] ;
   double* r = solver.getRPntr() ;
   double* J = solver.getJPntr() ;
   //
   solver._crj.computeRJ(r, J, x); // broyden.computeRJ(r, J, x);

   int npoints=40;
   Test_SNLSBroyden_GPU(npoints);
}
#endif
