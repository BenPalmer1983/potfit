
#include "potfit.h"

/******************************************************************************
*
* read potential table
*
******************************************************************************/

void read_pot_table( pot_table_t *pt, char *filename, int ncols )
{
  FILE *infile;
  char buffer[1024], msg[255], *res;
  int  have_format=0, end_header=0;
  int  format, size, i, j, *nvals;
  real *val;

  /* open file */
  infile = fopen(filename,"r");
  if (NULL == infile) {
    sprintf(msg,"Could not open file %s\n",filename);
    error(msg);
  }

  /* read the header */
  do {
    /* read one line */
    res=fgets(buffer,1024,infile);
    if (NULL == res) {
      sprintf(msg,"Unexpected end of file in %s",filename);
      error(msg);
    }
    /* check if it is a header line */
    if (buffer[0]!='#') {
      sprintf(msg,"Header corrupt in file %s",filename);
      error(msg);
    }
    /* stop after last header line */
    if (buffer[1]=='E') {
      end_header = 1;
    }
    /* see if it is the format line */
    else if (buffer[1]=='F') {
      /* format complete? */
      if (2!=sscanf( (const char*)(buffer+2), "%d %d", &format, &size )) {
        sprintf(msg,"Corrupt format header line in file %s",filename);
        error(msg);
      }
      /* right number of columns? */
      if (size!=ncols) {
        sprintf(msg,"Wrong number of data columns in file %s",filename);
        error(msg);
      }
      /* recognized format? */
      if (format!=3) {
        sprintf(msg,"Unrecognized format specified for file %s",filename);
        error(msg);
      }
      have_format = 1;
    }
  } while (!end_header);

  /* did we have a format in the header? */
  if (!have_format) {
    sprintf(msg,"Format not specified in header of file %s",filename);
    error(msg);
  }

  /* allocate info block of function table */
  pt->len     = 0;
  pt->ncols   = size;
  pt->begin   = (real *) malloc(size*sizeof(real));
  pt->end     = (real *) malloc(size*sizeof(real));
  pt->step    = (real *) malloc(size*sizeof(real));
  pt->invstep = (real *) malloc(size*sizeof(real));
  pt->first   = (int  *) malloc(size*sizeof(int ));
  pt->last    = (int  *) malloc(size*sizeof(int ));
  nvals       = (int  *) malloc(size*sizeof(int ));
  if ((pt->begin   == NULL) || (pt->end   == NULL) || (pt->step == NULL) || 
      (pt->invstep == NULL) || (pt->first == NULL) || (pt->last == NULL) || 
      (nvals       == NULL)) {
    sprintf(msg,"Cannot allocate info block for potential table %s",filename);
    error(msg);
  }

  /* read the info block of the function table */
  for(i=0; i<ncols; i++) {
    if (3>fscanf(infile,"%lf %lf %d", &pt->begin[i], &pt->end[i], &nvals[i])) {
        sprintf(msg, "Premature end of potential file %s", filename);
        error(msg);
    }
    pt->step[i] = (pt->end[i] - pt->begin[i]) / (nvals[i]-1);
    pt->invstep[i] = 1.0 / pt->step[i];
    if (i==0) pt->first[i] = 0; else pt->first[i] = pt->last[i-1] + 1;
    pt->last[i] = pt->first[i] + nvals[i] - 1;
    pt->len = pt->first[i] + nvals[i];
  }

  /* allocate the function table */
  pt->table = (real *) malloc(pt->len * sizeof(real));
  if (NULL==pt->table) {
    error("Cannot allocate memory for potential table");
  }

  /* input loop */
  val = pt->table;
  for (i=0; i<ncols; i++) {
    for (j=0; j<nvals[i]; j++) {
      if (1>fscanf(infile, "%lf\n", val)) {
        sprintf(msg, "Premature end of potential file %s", filename);
        error(msg);
      } else val++;
    }
  }

  fclose(infile);
}


/*****************************************************************************
*
*  Evaluate derivative of potential with quadratic interpolation. 
*  col is typ1 * ntypes + typ2.
*
******************************************************************************/

real grad2(pot_table_t *pt, int col, real r)
{
  real rr, istep, chi, p0, p1, p2, dv, d2v;
  int  k;

  /* check for distances shorter than minimal distance in table */
  rr = r - pt->begin[col];
  if (rr < 0) {
    rr   = 0;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = (int) (rr * istep);
  chi   = (rr - k * pt->step[col]) * istep;
  k    += pt->first[col];

  /* intermediate values */
  p0  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  p1  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  p2  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  dv  = p1 - p0;
  d2v = p2 - 2 * p1 + p0;

  /* return the derivative */
  return istep * (dv + (chi - 0.5) * d2v);
}


/*****************************************************************************
*
*  Evaluate potential with quadratic interpolation. 
*  col is typ1 * ntypes + typ2.
*
******************************************************************************/

real pot2(pot_table_t *pt, int col, real r)
{
  real rr, istep, chi, p0, p1, p2, dv, d2v;
  int  k;

  /* check for distances shorter than minimal distance in table */
  rr = r - pt->begin[col];
  if (rr < 0) {
    rr   = 0;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = (int) (rr * istep);
  chi   = (rr - k * pt->step[col]) * istep;
  k    += pt->first[col];

  /* intermediate values */
  p0  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  p1  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  p2  = (k<=pt->last[col]) ? pt->table[k++] : 0.0;
  dv  = p1 - p0;
  d2v = p2 - 2 * p1 + p0;

  /* return the potential value */
  return p0 + chi * dv + 0.5 * chi * (chi - 1) * d2v;
}


/*****************************************************************************
*
*  write potential table (format 3)
*
******************************************************************************/

void write_pot_table(pot_table_t *pt, char *filename)
{
  FILE *outfile;
  char msg[255];
  int  i, j;

  /* open file */
  outfile = fopen(filename,"w");
  if (NULL == outfile) {
    sprintf(msg,"Could not open file %s\n",filename);
    error(msg);
  }

  /* write header */
  fprintf(outfile, "#F 3 %d\n#E\n", pt->ncols ); 

  /* write info block */
  for (i=0; i<pt->ncols; i++) {
    fprintf(outfile, "%f %f %d\n", 
            pt->begin[i], pt->end[i], pt->last[i] - pt->first[i] + 1);
  }
  fprintf(outfile, "\n");

  /* write data */
  for (i=0; i<pt->ncols; i++) {
    for (j=pt->first[i]; j<=pt->last[i]; j++)
      fprintf(outfile, "%f\n", pt->table[j] );
    fprintf(outfile, "\n");
  }
  fclose(outfile);
}


/*****************************************************************************
*
*  write potential table for IMD (format 2)
*
******************************************************************************/

void write_pot_table_imd(pot_table_t *pt, char *filename)
{
  FILE *outfile;
  char msg[255];
  real *r2begin, *r2end, *r2step, r2;
  int  i, j, k, col1, col2;

  /* allocate memory */
  r2begin = (real *) malloc( ntypes * ntypes *sizeof(real) );
  r2end   = (real *) malloc( ntypes * ntypes *sizeof(real) );
  r2step  = (real *) malloc( ntypes * ntypes *sizeof(real) );
  if ((r2begin==NULL) || (r2end==NULL) || (r2step==NULL)) 
    error("Cannot allocate memory in  write_pot_table_imd");

  /* open file */
  outfile = fopen(filename,"w");
  if (NULL == outfile) {
    sprintf(msg,"Could not open file %s\n",filename);
    error(msg);
  }

  /* write header */
  fprintf(outfile, "#F 2 %d\n#E\n", ntypes * ntypes ); 

  /* write info block */
  for (i=0; i<ntypes; i++) 
    for (j=0; j<ntypes; j++) {
      col1 = i<j ? i * ntypes + j : j * ntypes + i;
      col2 = i * ntypes + j;
      r2begin[col2] = SQR(pt->begin[col1]);
      r2end  [col2] = SQR(pt->end[col1] + pt->step[col1]);
      r2step [col2] = (r2end[col2] - r2begin[col2]) / imdpotsteps;
      fprintf(outfile, "%f %f %f\n", r2begin[col2], r2end[col2], r2step[col2]);
    }
  fprintf(outfile, "\n");

  /* write data */
  for (i=0; i<ntypes; i++) 
    for (j=0; j<ntypes; j++) {
      col1 = i<j ? i * ntypes + j : j * ntypes + i;
      col2 = i * ntypes + j;
      r2 = r2begin[col2];
      for (k=0; k<imdpotsteps+5; k++) {
        fprintf(outfile, "%f\n", pot2(pt, col1, sqrt(r2) ));
	r2 += r2step[col2];
      }
    }
  fprintf(outfile, "\n");
  fclose(outfile);
}

