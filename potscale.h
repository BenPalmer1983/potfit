/****************************************************************
* 
*  potscale.h: potscale header file
*
*****************************************************************/


/****************************************************************
* $Revision: 1.2 $
* $Date: 2004/02/20 12:10:05 $
*****************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#define DUMMY_COL_RHO 0
/******************************************************************************
*
*  type definitions
*
******************************************************************************/

typedef double real;
typedef struct { real x; real y; real z; } vektor;

typedef struct {
  real *begin;      /* first value in the table */
  real *end;        /* last value in the table */
  real *step;       /* table increment */
  real *invstep;    /* inverse of increment */
  int  *first;      /* index of first entry */
  int  *last;       /* index of last entry */
  int  len;         /* total length of the table */
  int  idxlen;      /* number of changeable potential values */
  int  ncols;       /* number of columns */
  real *table;      /* the actual data */
  real *d2tab;      /* second derivatives of table data for spline int */
  int  *idx;        /* indirect indexing */
} pot_table_t;

#define MAX(a,b)   ((a) > (b) ? (a) : (b))
#define MIN(a,b)   ((a) < (b) ? (a) : (b))
#define SQR(a)     ((a)*(a))
#define SPROD(a,b) (((a).x * (b).x) + ((a).y * (b).y) + ((a).z * (b).z))

/******************************************************************************
*
*  global variables
*
******************************************************************************/

/* MAIN is defined only once in the main module */
#ifdef MAIN 
#define EXTERN             /* define Variables in main */
#define INIT(data) =data   /* initialize data only in main */
#else
#define EXTERN extern      /* declare them extern otherwise */
#define INIT(data)         /* skip initialization otherwise */
#endif
EXTERN real   pi       INIT(0.);
EXTERN int    ndim     INIT(0);
EXTERN int    ndimtot  INIT(0);
EXTERN int    ntypes   INIT(1);          /* number of atom types */
EXTERN int    eam      INIT(1);          /* EAM always used */
EXTERN int    opt      INIT(0);
EXTERN int    seed     INIT(0);
EXTERN real   anneal_temp INIT(0.);
EXTERN char startpot[255];               /* file with start potential */
EXTERN char endpot[255];                 /* file for end potential */
EXTERN char imdpot[255];                 /* file for IMD potential */
EXTERN char config[255];                 /* file with atom configuration */
EXTERN char plotfile[255];               /* file for plotting */
EXTERN char flagfile[255] INIT("potfit.break");
				         /* break if file exists */
EXTERN  char tempfile[255] INIT("\0");   /* backup potential file */
EXTERN char plotpointfile[255] INIT("\0");
                                         /* write points for plotting */
EXTERN int  imdpotsteps;                 /* resolution of IMD potential */
EXTERN pot_table_t pair_pot;             /* the potential table */
EXTERN int plot INIT(0);                 /* plot output flag */
EXTERN real *rcut INIT(NULL);
EXTERN real *rmin INIT(NULL);
EXTERN int  *idx INIT(NULL);
EXTERN real   *dummy_phi INIT(NULL);     /* Dummy Constraints for PairPot */
EXTERN real   dummy_rho INIT(1.);        /* Dummy Constraint for rho */
EXTERN real   dummy_r  INIT(2.5);        /* Distance of Dummy Constraints */


/******************************************************************************
*
*  function prototypes
*
******************************************************************************/

void error(char*);
void warning(char*);
void read_parameters(int, char**);
void read_paramfile(FILE*);
void read_pot_table(pot_table_t*, char*, int);
void write_pot_table(pot_table_t*, char*);
void write_pot_table_imd(pot_table_t*, char*);
void write_plotpot_pair(pot_table_t*, char*);
real grad2(pot_table_t*, real*, int, real);
real grad3(pot_table_t*, real*, int, real);
real pot2 (pot_table_t*, int, real);
real pot3 (pot_table_t*, int, real);
void spline_ed(real xstep, real y[], int n, real yp1, real ypn, real y2[]);
real splint_ed(pot_table_t *pt, real *xi,int col, real r);
real splint_grad_ed(pot_table_t *pt, real *xi, int col, real r);
real splint_comb_ed(pot_table_t *pt, real *xi, int col, real r, real *grad);
#ifdef PARABEL
real parab_comb_ed(pot_table_t *pt,  real *xi, int col, real r, real *grad);
real parab_grad_ed(pot_table_t *pt,  real *xi, int col, real r);
real parab_ed(pot_table_t *pt,  real *xi, int col, real r);
#endif
void print_scaling(pot_table_t *pt);
void init_splines(pot_table_t *pt);
void rescale_pot(pot_table_t*, real, real, real*);
