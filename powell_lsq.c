
/******************************************************************************
*
*  PoLSA - Powell Least Square Algorithm
*
******************************************************************************/

/******************************************************************************
*
* This utility will optimize a parameter vector xi[N] to minimize the sum of 
* squares U=sum_{i=1..M}(f_i(xi)-F_i)^2, where F_i is an array passed to the
* utility, and f_i(xi) is a function of the vector xi.
*
******************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "nrutil.h"
#include "powell_lsq.h"
#define NDIM 5
#define MDIM 9
#define XISTART  {1.757,1.419,.6725,-.5542,-3.367}
#define FSOLL  {3.85,2.95,2.63,2.33,2.24,2.05,1.82,1.80,1.75} 
/*#define XISTART {-1.2,1.}*/
/*#define FSOLL {0.,0.}*/
#define EPS .001
#define PRECISION 1.E-7
/* Well, almost nothing */
#define NOTHING 1.E-9
#define INNERLOOPS 61
#define TOOBIG 10000

double fsoll[MDIM] = FSOLL;  /*Value of forces from experiment*/
int fcalls=0;
double (*force_calc)(double []);

void powell_lsq(double *xi, double *fcalc(double[]))
{
   int i,j,k,m;                    /*Simple counting variables*/
   /* int n = NDIM; */                 /*Dimension of parameter space*/
   /* int m = MDIM; */                /*Dimension of force space*/
   /* double epsilon = EPS;*/            /*small increment for differentiation*/
   /* static double xi[NDIM]=XISTART; */  /*starting value of xi*/
   /* static double force[MDIM]; */
    double force_xi[MDIM];              /*calculated force, alt*/
    double **d;                          /*Direction vectors*/
    double **gamma;                      /*Matrix of derivatives*/
    double **lineqsys;                   /*Lin.Eq.Sys. Matrix*/
    double **les_inverse;		 /*Inverse of the lineqsys */
    double *delta;			 /*Vector pointing into correct dir'n*/
    double *delta_norm;			 /*Normalized vector delta */
    double *fxi1,*fxi2;			 /*last two function values */
    int *perm_indx;			 /*Keeps track of LU pivoting */
    double p[NDIM];                  /*Vectors needed in Powell's algorithm*/
    double q[NDIM];
   /* double deltaforce[MDIM];         Difference between calc &  set force */
    double perm_sig;
    double F,F2,F3,df,xi1,xi2;
    double temp,temp2;
    double norm_delta,norm_delta2=0.;
    
    d=dmatrix(0,NDIM-1,0,NDIM-1);
    gamma=dmatrix(0,MDIM-1,0,NDIM-1);
    lineqsys=dmatrix(0,NDIM-1,0,NDIM-1);
    les_inverse=dmatrix(0,NDIM-1,0,NDIM-1);
    perm_indx=ivector(0,NDIM-1);
    delta=dvector(0,NDIM-1);
    delta_norm=dvector(0,NDIM-1);
    fxi1=dvector(0,MDIM-1);
    fxi2=dvector(0,MDIM-1);
    force_calc=fcalc;
    
    /*(void) fxi_init(xi,fsoll,force_xi,NDIM, MDIM); init force_xi*/
    F=force_calc(xi,fxi1,NDIM,MDIM);
    (void) copy_vector(fxi1,force_xi,MDIM);
    do { /*outer loop, includes recalculating gamma*/
    	m=0;
    	(void) gamma_init(gamma, d, xi, fxi1, NDIM, MDIM);     /*init gamma*/
    	(void) lineqsys_init(gamma,lineqsys,fxi1,p,NDIM,MDIM); /*init lineqsys*/
    	F3=F;
    	do {
            (void) copy_matrix(lineqsys,les_inverse,NDIM,NDIM);
            (void) ludcmp_d(les_inverse,NDIM,perm_indx,&perm_sig);
            (void) copy_vector(p,q,NDIM);
            (void) lubksb_d(les_inverse,NDIM,perm_indx,q); 
            (void) mprove_d(lineqsys,les_inverse,NDIM,perm_indx,p,q);  
            (void) matdotvec(d,q,delta,NDIM,NDIM);
            (void) copy_vector(delta, delta_norm, NDIM);
	    norm_delta=normalize_vector(delta_norm,NDIM);
	    printf("%f ",norm_delta);
	    /* Exit if norm of vector delta "explodes" - 
	    Matrix probably became unstable */
	    if ((norm_delta>100.*norm_delta2) && (m>1)) 
		    break;
	    else 
		    norm_delta2=norm_delta;
            
	    F2=F;   /*shift F*/
	    F=linmin_d(xi,delta,F,NDIM,MDIM,&xi1,&xi2,fxi1,fxi2,force_calc);
	    /*printf("F(%d):=%1.10f; xi%d:=[%1.10f, %1.10f]; %1.10f;
	    \n",m,F,m,xi[0],xi[1],d[0][0]*d[0][1]+d[1][0]*d[1][1]);*/
	    printf("%d %f %f %f %f %f %f %d \n",
			    m,F,xi[0],xi[1],xi[2],xi[3],xi[4],fcalls);
    
        /* Find maximal p[i]*q[i]*/
            j=0;temp2=0.;
            for (i=0;i<NDIM;i++) 
                if ((temp=fabs(p[i]*q[i]))>temp2) {j=i;temp2=temp;};
            /*set new d[j]*/
            for (i=0;i<NDIM;i++) d[i][j]=delta_norm[i];
	    
	    /* Check for degeneracy in directions */
	    for (i=0;i<NDIM;i++) {
		if (i!=j) {
		    temp=0;
	            for (k=0;k<NDIM;k++) temp+=d[k][i]*d[k][j];
		    if (1-temp<=.0001) m=INNERLOOPS;  /*Emergency exit*/
	        }
	    }
	    /*update gamma, but if fn returns 1, matrix will be sigular, 
	    break inner loop and restart with new matrix*/
            if (gamma_update(gamma,xi1,xi2,fxi1,fxi2,j,NDIM,MDIM)) break;
            (void) lineqsys_update(gamma,lineqsys,fxi1,p,j,NDIM,MDIM);
	    m++;           /*increment loop counter*/
	/* loop at least 2*NDIM times, but at most INNERLOOPS or until no
	further improvement */ 
	    df=F2-F;  
        } while ((m<NDIM+1 || (m<=INNERLOOPS && df>PRECISION) ) &&
			df<TOOBIG); 
    /*End fit if whole series didn't improve F F*/
    } while (F3-F>PRECISION/1.);
    /*normalize and store delta before passing on to linmin */
    /*(void) minverse(lineqsys,les_inverse,NDIM,perm_indx,&perm_sign);*/
    free_dmatrix(d,0,NDIM-1,0,NDIM-1);
    free_dmatrix(gamma,0,MDIM-1,0,NDIM-1);
    free_dmatrix(lineqsys,0,NDIM-1,0,NDIM-1);
    free_dmatrix(les_inverse,0,NDIM-1,0,NDIM-1);
    free_ivector(perm_indx,0,NDIM-1);
    free_dvector(delta,0,NDIM-1);
    free_dvector(delta_norm,0,NDIM-1);
    
    return 0;
}

/******************************************************************
*
* fxi_init: Set force_xi and deltaforce from force_calc results
*
******************************************************************/

/* double fxi_init(double *xi, double *fsoll, double *force_xi, int n, int m){
    static double force[MDIM];
    int j;
    (void) force_calc(xi,force,n,m);
    for (j=0;j<MDIM;j++) force_xi[j]=force[j];
    return;
}
 */

/******************************************************************
 *
 * force_calc: Get value of force at place xi
 *
 ******************************************************************/

double force_calc(double *xi, double *force, int n, int m) {
    int j;
    double temp=0., f=0.0;
    fcalls++;
    /* Chemistry problem */
    force[0]=xi[0]+xi[1]*exp(xi[3]*0.0)+xi[2]*exp(xi[4]*0.);
    force[1]=xi[0]+xi[1]*exp(xi[3]*0.5)+xi[2]*exp(xi[4]*.5);
    force[2]=xi[0]+xi[1]*exp(xi[3]*1.0)+xi[2]*exp(xi[4]*1.);
    force[3]=xi[0]+xi[1]*exp(xi[3]*1.5)+xi[2]*exp(xi[4]*1.5);
    force[4]=xi[0]+xi[1]*exp(xi[3]*2.0)+xi[2]*exp(xi[4]*2.);
    force[5]=xi[0]+xi[1]*exp(xi[3]*3.0)+xi[2]*exp(xi[4]*3.);
    force[6]=xi[0]+xi[1]*exp(xi[3]*5.0)+xi[2]*exp(xi[4]*5.);
    force[7]=xi[0]+xi[1]*exp(xi[3]*8.0)+xi[2]*exp(xi[4]*8.);
    force[8]=xi[0]+xi[1]*exp(xi[3]*10.)+xi[2]*exp(xi[4]*10.); 

    
/* navigation problem */
/*    force[0]=atan((xi[1]-6.)/(xi[0]-8.));
    force[1]=atan((xi[1]-5.)/(xi[0]+4.));
    force[2]=atan((xi[1]+3.)/(xi[0]-1.));*/
		    
    
    
    /* Pyramidal Problem */
    /*force[0]=xi[0];
    force[1]=sqrt(2.)*xi[0];
    force[2]=xi[1];
    force[3]=sqrt(0.5*xi[0]*xi[0]+xi[1]*xi[1]);
    force[4]=sqrt(0.25*xi[0]*xi[0]+xi[1]*xi[1]);*/
		            
    /* Peter's Problem */
    /*force[0]=xi[0]-1.2;   
    force[1]=xi[0]*exp(xi[1]*log(4.))-2.5; 
    force[2]=xi[0]*exp(xi[1]*log(0.25))-0.63; */
    
		    
    /*Rosenbrock's Problem*/
/*    force[0]=10*(xi[1]-xi[0]*xi[0]);
    force[1]=1-xi[0];		    */

    for (j=0;j<m;j++) {
	temp=(force[j]-=fsoll[j]);
	f+=temp*temp;
    }
    return f;
}

/******************************************************************
*
* gamma_init: (Re-)Initialize gamma[j][i] (Gradient Matrix) after
*            (Re-)Start or whenever necessary by calculating numerical
*            gradients in coordinate directions. Includes re-setting the
*            direction vectors to coordinate directions.
*
******************************************************************/

void gamma_init(double **gamma, double **d, double *xi, double *force_xi, int n,
         	int m){
    double *force;
    int i,j;			/* Auxiliary vars: Counters */
    double sum,temp;			/* Auxiliary var: Sum */
/*   Set direction vectors to coordinate directions d_ij=KroneckerDelta_ij */
    /*Initialize direction vectors*/
    for (i=0;i<n;i++) {
	    for (j=0;j<n;j++) if (i==j) d[i][j]=1.; else d[i][j]=0.;
    }
/* Initialize gamma by calculating numerical derivatives    */
    force=dvector(0,m-1);
    for (i=0;i<n;i++) {           /*initialize gamma*/
	xi[i]+=EPS;                /*increase xi[i]...*/
	(void) force_calc(xi,force,n,m);
       	for (j=0;j<m;j++) gamma[j][i]=(force[j]-force_xi[j])/EPS;
	xi[i]-=EPS;                /*...and decrease xi[i] again*/
    }
    free_dvector(force,0,m-1);
/* scale gamma so that sum_j(gamma^2)=1                      */
    
    for (i=0;i<n;i++) {
	sum =0.;
	for (j=0;j<m;j++) sum += gamma[j][i]*gamma[j][i];
	temp=sqrt(sum);
	for (j=0;j<m;j++) gamma[j][i] /= temp; /*normalize gamma*/
    }
    return;
}

/*******************************************************************
*
* gamma_update: Update column j of gamma ( to newly calculated 
*           numerical derivatives (calculated from fa, fb 
*           at a,b); normalize new vector.
*
*******************************************************************/

int  gamma_update(double **gamma, double a, double b, double *fa, double *fb,
		int j, int n, int m) {
    int i;
    double temp;
    double sum=0.;
    for (i=0;i<m;i++) {
	    temp=gamma[i][j]=((fa[i]-fb[i])/(a-b));
	    sum+=temp*temp;
    }
    temp=sqrt(sum);               /* normalization factor */
    if (temp>NOTHING)
    	for (i=0;i<m;i++) gamma[i][j]/=temp; 
    else
	return 1;  /*Matrix will be singular: Restart!*/
    return 0;
}
		
/*******************************************************************
*
* lineqsys_init: Initialize LinEqSys matrix, vector p in 
*              lineqsys . q == p
*
*******************************************************************/

void lineqsys_init(double **gamma, double **lineqsys, double *deltaforce, 
	double *p, int n, int m){
    int i,j,k;			/* Auxiliary vars: Counters */

    /* calculating vector p (lineqsys . q == P in LinEqSys) */

    for (i=0;i<n;i++) {
	p[i]=0.;
	for (j=0;j<m;j++) {
	    p[i]-=gamma[j][i]*deltaforce[j];
	}
    }
    /* calculating the linear equation system matrix gamma^t.gamma */
    for (i=0;i<n;i++) {
	for (k=0;k<n;k++) {
	    lineqsys[i][k]=0;
	    for (j=0;j<m;j++) {
		lineqsys[i][k]+=gamma[j][i]*gamma[j][k];
	    }
/*	    printf("%f ", lineqsys[i][k]);*/
	}
/*	printf("\n");*/
    }
    return;
}

/*******************************************************************
*
* lineqsys_update: Update LinEqSys matrix line and column i, vector
*            p component i.
*
*******************************************************************/

void lineqsys_update(double **gamma, double **lineqsys, double *force_xi,
		double *p, int i, int n, int m){
    int j,k;
    double temp;
    for (k=0;k<n;k++){
    	p[k]=0.;
    	for (j=0;j<m;j++) p[k]-=gamma[j][k]*force_xi[j];
    }
    /* divide calculation of new line i and column i in 3 parts*/ 
    for (k=0;k<i;k++) {            /*Part 1: All elements ki and ik with k<i*/
        lineqsys[i][k]=0.;
	lineqsys[k][i]=0.;
	for (j=0;j<m;j++) {
	    temp=gamma[j][i]*gamma[j][k];
	    lineqsys[i][k]+=temp;
	    lineqsys[k][i]+=temp;
        }
    }
    lineqsys[i][i]=0.;              /*Part 2: Element ii */
    for (j=0;j<m;j++) {
	temp=gamma[j][i];
	lineqsys[i][i]+=temp*temp;
    }
    for (k=i+1;k<n;k++){	    /* Part 3: All elements ki and ik with k>i*/
        lineqsys[i][k]=0;
	lineqsys[k][i]=0;
	for (j=0;j<m;j++) {
	    temp=gamma[j][i]*gamma[j][k];
	    lineqsys[i][k]+=temp;
	    lineqsys[k][i]+=temp;
        }
    }
    return;
}
	


/*******************************************************************
*
*  copy_matrix: Copies data from Matrix a into matrix b
*              (matrix dimension n x m)
*
*******************************************************************/

/*>>>>>>>>>>>>>>>  INLINING? <<<<<<<<<<<<<<*/

void copy_matrix(double **a, double **b, int n, int m) {
    int i,j;
    for (i=0; i<m; i++) {
        for (j=0; j<n; j++) {
	    b[j][i]=a[j][i];
        }
    }
    return;
}

/******************************************************************
*
* copy_vector: Copies data from vector a into vector b (both dim n)
*
******************************************************************/

/*>>>>>>>>>>>>>>>  INLINING? <<<<<<<<<<<<<<*/

void copy_vector(double *a, double *b, int n) {
    int i;
    for (i=0; i<n; i++) b[i]=a[i];
    return;
}

/******************************************************************
*
* matdotvec: Calculates the product of matrix a (n x m) with column 
* vector x (dim m), Result y (dim n). (A . x = m)
*
******************************************************************/

void matdotvec(double **a, double *x, double *y, int n, int m){
    int i, j;
    for (i=0; i<n; i++) {
	y[i]=0.;
	for (j=0; j<m; j++) y[i]+=a[i][j]*x[j];
    }
    return;
}

/*******************************************************************
*
* normalize_vector: Normalizes vector to |vec|^2=1, returns norm of old vector
*
*******************************************************************/

double normalize_vector(double *v, int n){
    int j;
    double temp,sum=0.0;
    for (j=0;j<n;j++) sum+=v[j]*v[j];
    temp=sqrt(sum);
    for (j=0;j<n;j++) v[j]/=temp;
    return temp;
}
