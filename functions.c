/****************************************************************
*
* functions.c: Routines and function calls used for analytic potentials
*
*****************************************************************/
/*
*   Copyright 2008-2009 Daniel Schopf
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
* $Revision: 1.10 $
* $Date: 2009/02/16 14:10:28 $
*****************************************************************/

#ifdef APOT

#include "potfit.h"
#include <mkl_vml.h>

/*****************************************************************************
*
* return the number of parameters for a specific analytic potential
*
******************************************************************************/

int apot_parameters(char *name)
{
  if (strcmp(name, "lj") == 0) {
    return 2;
  } else if (strcmp(name, "eopp") == 0) {
    return 6;
  } else if (strcmp(name, "morse") == 0) {
    return 3;
  } else if (strcmp(name, "softshell") == 0) {
    return 2;
  } else if (strcmp(name, "eoppexp") == 0) {
    return 6;
  } else if (strcmp(name, "meopp") == 0) {
    return 7;
  }

  /* template for new potential function called newpot */

  else if (strcmp(name, "newpot") == 0) {
    return 2;
  }

  /* end of template */

  return -1;
}

/*****************************************************************************
*
* assign function pointers to corresponding functions
*
******************************************************************************/

int apot_assign_functions(apot_table_t *apt)
{
  int   i;

  for (i = 0; i < apt->number; i++) {
    if (strcmp(apt->names[i], "lj") == 0) {
      apt->fvalue[i] = &lj_value;
    } else if (strcmp(apt->names[i], "eopp") == 0) {
      apt->fvalue[i] = &eopp_value;
    } else if (strcmp(apt->names[i], "morse") == 0) {
      apt->fvalue[i] = &morse_value;
    } else if (strcmp(apt->names[i], "softshell") == 0) {
      apt->fvalue[i] = &softshell_value;
    } else if (strcmp(apt->names[i], "eoppexp") == 0) {
      apt->fvalue[i] = &eoppexp_value;
    } else if (strcmp(apt->names[i], "meopp") == 0) {
      apt->fvalue[i] = &meopp_value;
    }

/* template for new potential function called newpot */

    else if (strcmp(apt->names[i], "newpot") == 0) {
      apt->fvalue[i] = &newpot_value;
    }

/* end of template */

    else
      return -1;
  }
  return 0;
}

/*****************************************************************************
*
* actual functions representing the analytic potentials
*
******************************************************************************/

/******************************************************************************
*
* lennard-jones potential
*
******************************************************************************/

void lj_value(real r, real *p, real *f)
{
  real  sig_d_rad6, sig_d_rad12;

  sig_d_rad6 = (p[1] * p[1]) / (r * r);
  sig_d_rad6 = sig_d_rad6 * sig_d_rad6 * sig_d_rad6;
  sig_d_rad12 = sig_d_rad6 * sig_d_rad6;

  /* Lennard-Jones is 4*epsilon*((sigma/r)^12-(sigma/r)^6) */
  *f = 4 * p[0] * (sig_d_rad12 - sig_d_rad6);
}

/******************************************************************************
*
* empirical oscillating pair potential
*
******************************************************************************/

void eopp_value(real r, real *p, real *f)
{
  static real x[2], y[2], power[2];

  x[0] = r;
  x[1] = r;
  y[0] = p[1];
  y[1] = p[3];
  vdPow(2, x, y, power);

  *f = p[0] / power[0] + (p[2] / power[1]) * cos(p[4] * r + p[5]);
}

/******************************************************************************
*
* morse potential
*
******************************************************************************/

void morse_value(real r, real *p, real *f)
{
  *f = p[0] * (exp(-2 * p[1] * (r - p[2])) - 2 * exp(-p[1] * (r - p[2])));
}

/******************************************************************************
*
* softshell potential
*
******************************************************************************/

void softshell_value(real r, real *p, real *f)
{
  static real x, y, power;

  x = p[0] / r;
  y = p[1];
  vdPow(1, &x, &y, f);
}

/******************************************************************************
*
* eopp_exp potential
*
******************************************************************************/

void eoppexp_value(real r, real *p, real *f)
{
  static real x, y, power;

  x = r;
  y = p[3];
  vdPow(2, &x, &y, &power);

  *f = p[0] * exp(-p[1] * r) + (p[2] / power) * cos(p[4] * r + p[5]);
}

/******************************************************************************
*
* meopp potential
*
******************************************************************************/

void meopp_value(real r, real *p, real *f)
{
  static real x[2], y[2], power[2];

  x[0] = r - p[6];
  x[1] = r;
  y[0] = p[1];
  y[1] = p[3];
  vdPow(2, x, y, power);

  *f = p[0] / power[0] + (p[2] / power[1]) * cos(p[4] * r + p[5]);
}

/******************************************************************************
* 
* template for new potential function called mypotential 
* for further information plase have a look at the online documentation
* 
* http://www.itap.physik.uni-stuttgart.de/~imd/potfit/potfit.html
*
******************************************************************************/

/* template for new function */

/******************************************************************************
*
* newpot potential
*
******************************************************************************/

void newpot_value(real r, real *p, real *f)
{
  *f = r * p[0] + p[1];
}

/* end of template */

/******************************************************************************
*
* end of analytic potentials
*
******************************************************************************/

/*****************************************************************************
*
* check if the given analytic potential is valid
*
******************************************************************************/

int apot_validate(int param_index, real new_val)
{
  int   pot_index = apot_table.idxpot[param_index], i;
  real  x;

  if (pot_index < apot_table.number) {

    /* check if potential vanishes at 3*cutoff */
    apot_table.fvalue[pot_index] (3 * apot_table.end[pot_index],
				  apot_table.values[pot_index], &x);

    if (fabs(x) > 10e-2) {
#ifdef DEBUG
      printf("Validate failed\n");
#endif
      return 0;
    }
  }
  return 1;
}

#endif
