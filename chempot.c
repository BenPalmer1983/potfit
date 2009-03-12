/****************************************************************
* 
*  chempot.c: calculation of the chemical potential
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
* $Revision: 1.2 $
* $Date: 2009/03/12 15:00:20 $
*****************************************************************/

#ifdef APOT

#include "potfit.h"

#define SWAP_REAL(x,y,t) t=x;x=y;y=t;

int swap_chem_pot(int i, int j)
{
  real  temp;

  if (i != j) {
    SWAP_REAL(apot_table.values[apot_table.number][i],
	      apot_table.values[apot_table.number][j], temp);
    SWAP_REAL(compnodelist[i - ntypes], compnodelist[j - ntypes], temp);
    SWAP_REAL(apot_table.pmin[apot_table.number][i],
	      apot_table.pmin[apot_table.number][j], temp);
    SWAP_REAL(apot_table.pmax[apot_table.number][i],
	      apot_table.pmax[apot_table.number][j], temp);
    return 0;
  } else
    return -1;
}

int sort_chem_pot_2d()
{
  /* bubble sort */
  int   i, swapped;

  if (compnodes > 0)
    do {
      swapped = 0;
      for (i = 0; i < (compnodes - 1); i++) {
	if (compnodelist[i] > compnodelist[i + 1]) {
	  swap_chem_pot(ntypes + i, ntypes + i + 1);
	  swapped = 1;
	}
      }
    } while (swapped);

  return 0;
}

real chemical_potential_1d(int *n, real *mu)
{
  return n[0] * mu[0];
}

real chemical_potential_2d(int *n, real *mu)
{
  real  nfrac;
  int   ntot;

  ntot = n[0] + n[1];
  nfrac = (real)n[1] / ntot;

  if (nfrac == 0 || nfrac == 1 || compnodes == 0) {
    return n[0] * mu[0] + n[1] * mu[1];
  }

  int   i;

  i = 0;
  while (nfrac > compnodelist[i] && i < compnodes) {
    i++;
  }

  real  xl, xr, yl, yr, temp;

  if (i == 0) {
    xl = 0;
    xr = compnodelist[0];
    yl = mu[0];
    yr = mu[2];
  } else if (i == compnodes) {
    xr = 1;
    xl = compnodelist[compnodes - 1];
    yl = mu[ntypes + compnodes - 1];
    yr = mu[1];
  } else {
    xl = compnodelist[i - 1];
    xr = compnodelist[i];
    yl = mu[ntypes + i - 2];
    yr = mu[ntypes + i - 1];
  }

  temp = (yr - yl) / (xr - xl);
  temp = (yl + (nfrac - xl) * temp);

  return temp * ntot;
}

real chemical_potential_3d(int *n, real *mu)
{
  return n[0] * mu[0] + n[1] * mu[1] + n[2] * mu[2];
}

void init_chemical_potential(int dim)
{
  if (dim == 2)
    sort_chem_pot_2d();
  if (dim == 3)
    printf
      ("Chemical potentials for n>=3 is not implemented.\nFalling back to N_i*mu_i\n");
}

real chemical_potential(int dim, int *n, real *mu)
{
  if (dim == 1)
    return chemical_potential_1d(n, mu);
  if (dim == 2)
    return chemical_potential_2d(n, mu);
  if (dim >= 3)
    return chemical_potential_3d(n, mu);
  return 0.;
}

#endif
