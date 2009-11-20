/****************************************************************
*
*  diff_evo.c: Implementation of the differential evolution
* 		algorithm for global optimization
*
*****************************************************************/
/*
*   Copyright 2009 Daniel Schopf
*             Institute for Theoretical and Applied Physics
*             University of Stuttgart, D-70550 Stuttgart, Germany
*             http://www.itap.physik.uni-stuttgart.de/
*
*****************************************************************/
/*
*   This file is part of potfit.
*
*   potfit is free software; you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation; either version 2 of the License, or
*   (at your option) any later version.
*
*   potfit is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with potfit; if not, write to the Free Software
*   Foundation, Inc., 51 Franklin St, Fifth Floor,
*   Boston, MA  02110-1301  USA
*/
/****************************************************************
* $Revision: 1.1 $
* $Date: 2009/11/20 08:21:58 $
*****************************************************************/

#if defined EVO

#include <math.h>
#include "potfit.h"
#include "utils.h"

#ifdef APOT
#define D ndim
#else
#define D ndimtot
#endif

/* parameters for the differential evolution algorithm */
#define NP 5*ndim		// number of total population
#define CR 0.5			// crossover constant, in [0,1]
#define F 0.2			// coupling constant with others, in [0,1]

#define KILL_MAX 0		// kill the worst configuration?
#define MAX_LOOPS 1e6		// max number of loops performed
#define MAX_UNCHANGED 1000	// abort after number of unchanged steps
#define RAND_MAX 2147483647

#ifdef APOT

/****************************************************************
 *
 *  real normdist(): Returns a normally distributed random variable
 * 	Uses random() to generate a random number.
 *
 *****************************************************************/

real normdist()
{
  static int have = 0;
  static real nd2;
  real  x1, x2, sqr, cnst;

  if (!(have)) {
    do {
      x1 = 2.0 * random() / (RAND_MAX + 1.0) - 1.0;
      x2 = 2.0 * random() / (RAND_MAX + 1.0) - 1.0;
      sqr = x1 * x1 + x2 * x2;
    } while (!(sqr <= 1.0 && sqr > 0));
    /* Box Muller Transformation */
    cnst = sqrt(-2.0 * log(sqr) / sqr);
    nd2 = x2 * cnst;
    have = 1;
    return x1 * cnst;
  } else {
    have = 0;
    return nd2;
  }
}

real *calc_vect(real *x)
{
  int   i, j, k = 0, n = 0;
  static real *vect;

  if (vect == NULL)
    vect = (real *)malloc(ndimtot * sizeof(real));

  for (i = 0; i < apot_table.number; i++) {
    vect[k++] = 0;
    vect[k++] = 0;
    for (j = 0; j < apot_table.n_par[i]; j++) {
      vect[k++] = x[n++];
    }
  }
  return vect;
}

#endif

void init_population(real **pop, real *xi, int size, real scale)
{
  int   i, j;
  real  temp, max, min, val;

  for (i = 0; i < size; i++)
#ifdef APOT
    pop[0][i] = xi[idx[i]];
#else
    pop[0][i] = xi[i];
#endif
  for (i = 1; i < NP; i++) {
    for (j = 0; j < size; j++) {
#ifdef APOT
      val = xi[idx[j]];
      min = apot_table.pmin[apot_table.idxpot[j]][apot_table.idxparam[j]];
      max = apot_table.pmax[apot_table.idxpot[j]][apot_table.idxparam[j]];
#else
      val = xi[j];
      min = .9 * val;
      max = 1.1 * val;
#endif
      /* force normal distribution to [-1:1] or less */
      temp = normdist() / (3 * scale);
      if (temp > 0)
	pop[i][j] = val + temp * (max - val);
      else
	pop[i][j] = val + temp * (val - min);
    }
  }
}

void diff_evol(real *xi)
{
  int   a, b, c, d, e, i, j, k, count = 0, last_changed = 0, n_max;
  real  force, min = 10e10, max = 0, avg = 0, temp = 0, sum = 0, tmpsum =
    0, pmin = 0, pmax = 0;
  real *cost, *fxi, *trial, *best, *opt;
  real **x1, **x2;

  fxi = vect_real(mdim);

  trial = (real *)malloc(D * sizeof(real));

  x1 = (real **)malloc(NP * sizeof(real *));
  x2 = (real **)malloc(NP * sizeof(real *));
  cost = (real *)malloc(NP * sizeof(real));
  best = (real *)malloc(NP * sizeof(real));
  if (x1 == NULL || x2 == NULL || trial == NULL || cost == NULL)
    error("Could not allocate memory for population vector!\n");
  for (i = 0; i < NP; i++) {
    x1[i] = (real *)malloc(D * sizeof(real));
    x2[i] = (real *)malloc(D * sizeof(real));
    if (x1[i] == NULL || x2[i] == NULL)
      error("Could not allocate memory for population vector!\n");
  }

  if (KILL_MAX)
    warning("\nPopulation killing enabled. Be careful, this is untested!\n");
  init_population(x1, xi, D, 1);
  for (i = 0; i < NP; i++) {
#ifdef APOT
    opt = calc_vect(x1[i]);
#else
    opt = x1[i];
#endif
    cost[i] = (*calc_forces) (opt, fxi, 0);
    if (cost[i] < min) {
      min = cost[i];
      for (j = 0; j < D; j++)
	best[j] = x1[i][j];
    }
    if (cost[i] > max) {
      max = cost[i];
      n_max = i;
    }
  }

  printf("Starting Differential Evolution with the following parameters:\n");
  printf("D=%d, NP=%d, CR=%f, F=%f\n", D, NP, CR, F);
  printf("Loops\t\tOptimum\t\tAverage cost\n");
  printf("%8d\t%f\n", count, min);

  while (count < MAX_LOOPS && last_changed < MAX_UNCHANGED) {
    sum = 0;
    max = 0;
    for (i = 0; i < NP; i++) {
      tmpsum = 0;
      do
	a = (int)(1. * rand() / (RAND_MAX + 1.) * NP);
      while (a == i);
      do
	b = (int)(1. * rand() / (RAND_MAX + 1.) * NP);
      while (b == i || b == a);
      do
	c = (int)(1. * rand() / (RAND_MAX + 1.) * NP);
      while (c == i || c == a || c == b);
/*      do*/
/*        d = (int)(1. * rand() / (RAND_MAX + 1.) * NP);*/
/*      while (d == i || d == a || d == b || d == c);*/
/*      do*/
/*        e = (int)(1. * rand() / (RAND_MAX + 1.) * NP);*/
/*      while (e == i || e == a || e == b || e == c || e == d);*/
      j = (int)(1. * rand() / (RAND_MAX + 1.) * D);
      for (k = 1; k <= D; k++) {
	if ((1. * rand() / (RAND_MAX + 1.)) < CR || k == D) {
	  /* DE/rand/1/exp */
/*          temp = x1[c][j] + F * (x1[a][j] - x1[b][j]);*/
	  /* DE/best/1/exp */
/*          temp = best[j] + F * (x1[a][j] - x1[b][j]);*/
	  /* DE/rand/2/exp */
/*          temp = x1[e][j] + F * (x1[a][j] + x1[b][j] - x1[c][j] - x1[d][j]);*/
	  /* DE/best/2/exp */
/*          temp = best[j] + F * (x1[a][j] + x1[b][j] - x1[c][j] - x1[d][j]);*/
	  /* DE/rand-to-best/1/exp */
	  temp = x1[c][j] + (1 - F) * (best[j] - x1[c][j]) +
	    F * (x1[a][j] - x1[b][j]);
	  /* DE/rand-to-best/2/exp */
/*          temp = x1[e][j] + (1 - F) * (best[j] - x1[e][j]) +*/
/*            F * (x1[a][j] + x1[b][j] - x1[c][j] - x1[d][j]);*/
#ifdef APOT
	  pmin =
	    apot_table.pmin[apot_table.idxpot[j]][apot_table.idxparam[j]];
	  pmax =
	    apot_table.pmax[apot_table.idxpot[j]][apot_table.idxparam[j]];
	  if (temp > pmax || temp < pmin) {
	    trial[j] =
/*              pmin + (1. * random() / (RAND_MAX + 1.) * (pmax - pmin));*/
	      x1[(int)(1. * random() / (RAND_MAX + 1.) * D)][j];
	  } else
	    trial[j] = temp;
#else
	  trial[j] = temp;
#endif
	  tmpsum += fabs(x1[i][j] - temp);
	} else {
	  trial[j] = x1[i][j];
	}
	j = (j + 1) % D;
      }
#ifdef APOT
      opt = calc_vect(trial);
#else
      opt = trial;
#endif
      force = (*calc_forces) (opt, fxi, 0);
      if (force < min) {
	last_changed = 0;
	for (j = 0; j < D; j++)
	  best[j] = trial[j];
	if (tempfile != "\0") {
	  for (j = 0; j < ndim; j++)
#ifdef APOT
	    apot_table.values[apot_table.idxpot[j]][apot_table.idxparam[j]] =
	      trial[j];
	  write_pot_table(&apot_table, tempfile);
#else
	    xi[j] = trial[j];
	  write_pot_table(&opt_pot, tempfile);
#endif
	}
	min = force;
      }
      if (force > max) {
	max = force;
	n_max = i;
      }
      if (force <= cost[i]) {
	if (force < cost[i])
	  sum += tmpsum;
	for (j = 0; j < D; j++)
	  x2[i][j] = trial[j];
	cost[i] = force;
      } else
	for (j = 0; j < D; j++)
	  x2[i][j] = x1[i][j];
    }
    if (KILL_MAX) {
      for (j = 0; j < D; j++)
	x2[n_max][j] = best[j];
      cost[n_max] = min;
    }
    avg = 0;
    for (i = 0; i < NP; i++)
      avg += cost[i];
#ifdef APOT
    printf("%8d\t%f\t%f\t%f\n", count + 1, min, avg / NP, sum / (NP * D));
#else
    printf("%8d\t%f\t%f\n", count + 1, min, avg / NP);
#endif
    fflush(stdout);
    for (i = 0; i < NP; i++)
      for (j = 0; j < D; j++)
	x1[i][j] = x2[i][j];
    count++;
    last_changed++;
    if (last_changed == MAX_UNCHANGED) {
      printf
	("\nCould not find any improvements in the last %d steps.\n",
	 MAX_UNCHANGED);
      printf("Aborting evolution algorithm ...\n\n");
    }
  }

#ifdef APOT
  opt = calc_vect(best);
#else
  opt = best;
#endif
  for (j = 0; j < ndimtot; j++)
    xi[j] = opt[j];

  /* clean up */
  for (i = 0; i < NP; i++) {
    free(x1[i]);
    free(x2[i]);
  }
  free(x1);
  free(x2);
  free(trial);
  free(cost);
  free(best);
}

#endif
