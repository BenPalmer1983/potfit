/****************************************************************
 *
 * force_tersoff.c: Routines used for calculating Tersoff forces/energies
 *
 ****************************************************************
 *
 * Copyright 2002-2013
 *	Institute for Theoretical and Applied Physics
 *	University of Stuttgart, D-70550 Stuttgart, Germany
 *	http://potfit.sourceforge.net/
 *
 ****************************************************************
 *
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
 *   along with potfit; if not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************/

#ifdef TERSOFF

#include "potfit.h"

#include "functions.h"
#include "potential.h"
#include "splines.h"
#include "utils.h"

/****************************************************************
 *
 *  compute forces using pair potentials with spline interpolation
 *
 *  returns sum of squares of differences between calculated and reference
 *     values
 *
 *  arguments: *xi - pointer to potential
 *             *forces - pointer to forces calculated from potential
 *             flag - used for special tasks
 *
 * When using the mpi-parallelized version of potfit, all processes but the
 * root process jump into this function immediately after initialization and
 * stay in here for an infinite loop, to exit only when a certain flag value
 * is passed from process 0. When a set of forces needs to be calculated,
 * the root process enters the function with a flag value of 0, broadcasts
 * the current potential table xi and the flag value to the other processes,
 * thus initiating a force calculation. Whereas the root process returns with
 * the result, the other processes stay in the loop. If the root process is
 * called with flag value 1, all processes exit the function without
 * calculating the forces.
 * If anything changes about the potential beyond the values of the parameters,
 * e.g. the location of the sampling points, these changes have to be broadcast
 * from rank 0 process to the higher ranked processes. This is done when the
 * root process is called with flag value 2. Then a potsync function call is
 * initiated by all processes to get the new potential from root.
 *
 * xi_opt is the array storing the potential parameters (usually it is the
 *     opt_pot.table - part of the struct opt_pot, but it can also be
 *     modified from the current potential.
 *
 * forces is the array storing the deviations from the reference data, not
 *     only for forces, but also for energies, stresses or dummy constraints
 *     (if applicable).
 *
 * flag is an integer controlling the behaviour of calc_forces_pair.
 *    flag == 1 will cause all processes to exit calc_forces_pair after
 *             calculation of forces.
 *    flag == 2 will cause all processes to perform a potsync (i.e. broadcast
 *             any changed potential parameters from process 0 to the others)
 *             before calculation of forces
 *    all other values will cause a set of forces to be calculated. The root
 *             process will return with the sum of squares of the forces,
 *             while all other processes remain in the function, waiting for
 *             the next communication initiating another force calculation
 *             loop
 *
 ****************************************************************/

double calc_forces_tersoff(double *xi_opt, double *forces, int flag)
{
  double tmpsum = 0.0, sum = 0.0;
  const tersoff_t *tersoff = &apot_table.tersoff;

#ifndef MPI
  myconf = nconf;
#endif /* !MPI */

  /* This is the start of an infinite loop */
  while (1) {
    tmpsum = 0.;		/* sum of squares of local process */

#ifndef MPI
    apot_check_params(xi_opt);
#endif /* !MPI */

#ifdef MPI
    MPI_Bcast(&flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (flag == 1)
      break;			/* Exception: flag 1 means clean up */

    if (myid == 0)
      apot_check_params(xi_opt);
    MPI_Bcast(xi_opt, ndimtot, MPI_DOUBLE, 0, MPI_COMM_WORLD);
#endif /* MPI */

    update_tersoff_pointers(xi_opt);

    /* region containing loop over configurations */
    {
      atom_t *atom;		/* pointer to current atom */
      neigh_t *neigh_j;		/* pointer to current neighbor j (first neighbor loop) */
      neigh_t *neigh_k;		/* pointer to current neighbor k (second neighbor loop) */
      angl *n_angl;		/* pointer to current angular table */
      int   h;			/* counter for configurations */
      int   i;			/* counter for atoms */
      int   j;			/* counter for neighbors (first loop) */
      int   k;			/* counter for neighbors (second loop) */
      int   n_i;		/* index number of the ith atom */
      int   n_j;		/* index number of the jth atom */
      int   n_k;		/* index number of the kth atom */
      int   self, uf;
#ifdef STRESS
      int   us, stresses;
#endif /* STRESS */

      int   col_j, col_k;
      int   ijk;

      /* pair variables */
      double phi_val, phi_grad, phi_a;
      double cut_tmp, cut_tmp_j;
      double tmp_jk;
      double cos_theta, g_theta;
      double tmp_1, tmp_2, tmp_3, tmp_4, tmp_5, tmp_6, tmp_grad, tmp;
      double tmp_j2, tmp_k2;
      double b_ij;
      vector force_j, tmp_force;
      double zeta;
      double tmp_pow_1, tmp_pow_2;
      vector dzeta_i, dzeta_j;
      vector dcos_j, dcos_k;

      /* loop over configurations */
      for (h = firstconf; h < firstconf + myconf; h++) {
	uf = conf_uf[h - firstconf];

	/* reset energies and stresses */
	forces[energy_p + h] = 0.0;

#ifdef STRESS
	us = conf_us[h - firstconf];
	stresses = stress_p + 6 * h;
	for (i = 0; i < 6; i++)
	  forces[stresses + i] = 0.0;
#endif /* STRESS */

	/* first loop over all atoms: reset forces, densities */
	for (i = 0; i < inconf[h]; i++) {
	  if (uf) {
	    n_i = 3 * (cnfstart[h] + i);
	    forces[n_i + 0] = -force_0[n_i + 0];
	    forces[n_i + 1] = -force_0[n_i + 1];
	    forces[n_i + 2] = -force_0[n_i + 2];
	  } else {
	    n_i = 3 * (cnfstart[h] + i);
	    forces[n_i + 0] = 0.0;
	    forces[n_i + 1] = 0.0;
	    forces[n_i + 2] = 0.0;
	  }
	}
	/* end first loop over all atoms */

	/* second loop: calculate cutoff function f_c for all neighbors */
	for (i = 0; i < inconf[h]; i++) {
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  n_i = 3 * (cnfstart[h] + i);

	  /* loop over neighbors */
	  for (j = 0; j < atom->num_neigh; j++) {
	    neigh_j = atom->neigh + j;
	    col_j = neigh_j->col[0];
	    /* check if we are within the cutoff range */
	    if (neigh_j->r < *(tersoff->S[col_j])) {
	      self = (neigh_j->nr == i + cnfstart[h]) ? 1 : 0;

	      /* calculate cutoff function f_c and store it for every neighbor */
	      cut_tmp = M_PI / (*(tersoff->S[col_j]) - *(tersoff->R[col_j]));
	      cut_tmp_j = cut_tmp * (neigh_j->r - *(tersoff->R[col_j]));
	      if (neigh_j->r < *(tersoff->R[col_j])) {
		neigh_j->f = 1.0;
		neigh_j->df = 0.0;
	      } else {
		neigh_j->f = 0.5 * (1.0 + cos(cut_tmp_j));
		neigh_j->df = -0.5 * cut_tmp * sin(cut_tmp_j);
	      }

	      /* calculate pair part f_c*A*exp(-lambda*r) and the derivative */
	      tmp = exp(-*(tersoff->lambda[col_j]) * neigh_j->r);
	      phi_val = neigh_j->f * *(tersoff->A[col_j]) * tmp;
	      phi_grad = neigh_j->df - *(tersoff->lambda[col_j]) * neigh_j->f;
	      phi_grad *= *(tersoff->A[col_j]) * tmp;

	      /* avoid double counting if atom is interacting with a copy of itself */
	      if (self) {
		phi_val *= 0.5;
		phi_grad *= 0.5;
	      }

	      /* only half cohesive energy because we have a full neighbor list */
	      forces[energy_p + h] += 0.5 * phi_val;

	      if (uf) {
		/* calculate pair forces */
		tmp_force.x = neigh_j->dist_r.x * phi_grad;
		tmp_force.y = neigh_j->dist_r.y * phi_grad;
		tmp_force.z = neigh_j->dist_r.z * phi_grad;
		forces[n_i + 0] += tmp_force.x;
		forces[n_i + 1] += tmp_force.y;
		forces[n_i + 2] += tmp_force.z;
#ifdef STRESS
		/* also calculate pair stresses */
		if (us) {
		  forces[stresses + 0] -= 0.5 * neigh_j->dist.x * tmp_force.x;
		  forces[stresses + 1] -= 0.5 * neigh_j->dist.y * tmp_force.y;
		  forces[stresses + 2] -= 0.5 * neigh_j->dist.z * tmp_force.z;
		  forces[stresses + 3] -= 0.5 * neigh_j->dist.x * tmp_force.y;
		  forces[stresses + 4] -= 0.5 * neigh_j->dist.y * tmp_force.z;
		  forces[stresses + 5] -= 0.5 * neigh_j->dist.z * tmp_force.x;
		}
#endif /* STRESS */
	      }
	    } else {
	      neigh_j->f = 0.0;
	      neigh_j->df = 0.0;
	    }
	  }			/* loop over neighbors */

	  /* loop over neighbors */
	  /* calculate threebody part */
	  for (j = 0; j < atom->num_neigh; j++) {
	    neigh_j = atom->neigh + j;
	    col_j = neigh_j->col[0];
	    /* check if we are within the cutoff range */
	    if (neigh_j->r < *(tersoff->S[col_j])) {
	      ijk = neigh_j->ijk_start;
	      n_j = 3 * neigh_j->nr;

	      /* skip neighbor if coefficient is zero */
	      if (0.0 == *(tersoff->B[col_j]))
		continue;

	      /* reset variables for each neighbor */
	      zeta = 0.0;
	      dzeta_i.x = 0.0;
	      dzeta_i.y = 0.0;
	      dzeta_i.z = 0.0;
	      dzeta_j.x = 0.0;
	      dzeta_j.y = 0.0;
	      dzeta_j.z = 0.0;

	      /* inner loop over neighbors */
	      for (k = 0; k < atom->num_neigh; k++) {
		if (k == j)
		  continue;
		neigh_k = atom->neigh + k;
		col_k = neigh_k->col[0];
		n_angl = atom->angl_part + ijk++;
		if (neigh_k->r < *(tersoff->S[col_k])) {

		  tmp_jk = 1.0 / (neigh_j->r * neigh_k->r);
		  cos_theta = n_angl->cos;

		  tmp_1 = *(tersoff->h[col_j]) - cos_theta;
		  tmp_2 = 1.0 / (tersoff->d2[col_j] + tmp_1 * tmp_1);
		  g_theta = 1.0 + tersoff->c2[col_j] / tersoff->d2[col_j] - tersoff->c2[col_j] * tmp_2;

		  /* zeta */
		  zeta += neigh_k->f * *(tersoff->omega[col_k]) * g_theta;

		  tmp_j2 = cos_theta / (neigh_j->r * neigh_j->r);
		  tmp_k2 = cos_theta / (neigh_k->r * neigh_k->r);

		  dcos_j.x = tmp_jk * neigh_k->dist.x - tmp_j2 * neigh_j->dist.x;
		  dcos_j.y = tmp_jk * neigh_k->dist.y - tmp_j2 * neigh_j->dist.y;
		  dcos_j.z = tmp_jk * neigh_k->dist.z - tmp_j2 * neigh_j->dist.z;

		  dcos_k.x = tmp_jk * neigh_j->dist.x - tmp_k2 * neigh_k->dist.x;
		  dcos_k.y = tmp_jk * neigh_j->dist.y - tmp_k2 * neigh_k->dist.y;
		  dcos_k.z = tmp_jk * neigh_j->dist.z - tmp_k2 * neigh_k->dist.z;

		  tmp_3 = 2.0 * tersoff->c2[col_j] * tmp_1 * tmp_2 * tmp_2 * neigh_k->f *
		    *(tersoff->omega[col_k]);

		  tmp_grad = neigh_k->df / neigh_k->r * g_theta * *(tersoff->omega[col_k]);

		  neigh_k->dzeta.x = tmp_grad * neigh_k->dist.x - tmp_3 * dcos_k.x;
		  neigh_k->dzeta.y = tmp_grad * neigh_k->dist.y - tmp_3 * dcos_k.y;
		  neigh_k->dzeta.z = tmp_grad * neigh_k->dist.z - tmp_3 * dcos_k.z;

		  dzeta_i.x -= neigh_k->dzeta.x;
		  dzeta_i.y -= neigh_k->dzeta.y;
		  dzeta_i.z -= neigh_k->dzeta.z;

		  dzeta_j.x -= tmp_3 * dcos_j.x;
		  dzeta_j.y -= tmp_3 * dcos_j.y;
		  dzeta_j.z -= tmp_3 * dcos_j.z;
		}
	      }			/* k */

	      phi_a = 0.5 * *(tersoff->B[col_j]) * exp(-*(tersoff->mu[col_j]) * neigh_j->r);

	      tmp_pow_1 = *(tersoff->gamma[col_j]) * zeta;
	      power_1(&tmp_4, &tmp_pow_1, tersoff->n[col_j]);

	      tmp_pow_1 = 1.0 + tmp_4;
	      tmp_pow_2 = -1.0 / (2.0 * *(tersoff->n[col_j]));
	      power_1(&b_ij, &tmp_pow_1, &tmp_pow_2);

	      phi_val = -b_ij * phi_a;

	      forces[energy_p + h] += neigh_j->f * phi_val;

	      if (0.0 == zeta)
		tmp_5 = 0.0;
	      else
		tmp_5 = -b_ij * neigh_j->f * phi_a * tmp_4 / (2.0 * zeta * (1.0 + tmp_4));
	      tmp_6 =
		(neigh_j->f * phi_a * *(tersoff->mu[col_j]) * b_ij + neigh_j->df * phi_val) / neigh_j->r;

	      force_j.x = -tmp_6 * neigh_j->dist.x + tmp_5 * dzeta_j.x;
	      force_j.y = -tmp_6 * neigh_j->dist.y + tmp_5 * dzeta_j.y;
	      force_j.z = -tmp_6 * neigh_j->dist.z + tmp_5 * dzeta_j.z;

	      for (k = 0; k < atom->num_neigh; k++) {
		if (k != j) {
		  neigh_k = atom->neigh + k;
		  col_k = neigh_k->col[0];
		  if (neigh_k->r < *(tersoff->S[col_k])) {
		    n_k = 3 * neigh_k->nr;
		    /* update force on particle k */
		    forces[n_k + 0] += tmp_5 * neigh_k->dzeta.x;
		    forces[n_k + 1] += tmp_5 * neigh_k->dzeta.y;
		    forces[n_k + 2] += tmp_5 * neigh_k->dzeta.z;

		    /* Distribute stress among atoms */
#ifdef STRESS
		    if (us) {
		      tmp = neigh_k->dist.x * tmp_5 * neigh_k->dzeta.x;
		      forces[stresses + 0] -= tmp;
		      tmp = neigh_k->dist.y * tmp_5 * neigh_k->dzeta.y;
		      forces[stresses + 1] -= tmp;
		      tmp = neigh_k->dist.z * tmp_5 * neigh_k->dzeta.z;
		      forces[stresses + 2] -= tmp;
		      tmp =
			0.5 * tmp_5 * (neigh_k->dist.x * neigh_k->dzeta.y +
			neigh_k->dist.y * neigh_k->dzeta.x);
		      forces[stresses + 3] -= tmp;
		      tmp =
			0.5 * tmp_5 * (neigh_k->dist.y * neigh_k->dzeta.z +
			neigh_k->dist.z * neigh_k->dzeta.y);
		      forces[stresses + 4] -= tmp;
		      tmp =
			0.5 * tmp_5 * (neigh_k->dist.z * neigh_k->dzeta.x +
			neigh_k->dist.x * neigh_k->dzeta.z);
		      forces[stresses + 5] -= tmp;
		    }
#endif /* STRESS */

		  }
		}		/* k != j */
	      }			/* k loop */

	      /* update force on particle j */
	      forces[n_j + 0] += force_j.x;
	      forces[n_j + 1] += force_j.y;
	      forces[n_j + 2] += force_j.z;

	      /* update force on particle i */
	      forces[n_i + 0] += tmp_5 * dzeta_i.x - force_j.x;
	      forces[n_i + 1] += tmp_5 * dzeta_i.y - force_j.y;
	      forces[n_i + 2] += tmp_5 * dzeta_i.z - force_j.z;

#ifdef STRESS			/* Distribute stress among atoms */
	      if (us) {
		tmp = neigh_j->dist.x * force_j.x;
		forces[stresses + 0] -= tmp;
		tmp = neigh_j->dist.y * force_j.y;
		forces[stresses + 1] -= tmp;
		tmp = neigh_j->dist.z * force_j.z;
		forces[stresses + 2] -= tmp;
		tmp = 0.5 * (neigh_j->dist.x * force_j.y + neigh_j->dist.y * force_j.x);
		forces[stresses + 3] -= tmp;
		tmp = 0.5 * (neigh_j->dist.y * force_j.z + neigh_j->dist.z * force_j.y);
		forces[stresses + 4] -= tmp;
		tmp = 0.5 * (neigh_j->dist.z * force_j.x + neigh_j->dist.x * force_j.z);
		forces[stresses + 5] -= tmp;
	      }
#endif /* STRESS */

	    }			/* j */
	  }
	}
	/* end second loop over all atoms */

	/* third loop over all atoms, sum up forces */
	if (uf) {
	  for (i = 0; i < inconf[h]; i++) {
	    n_i = 3 * (cnfstart[h] + i);
#ifdef FWEIGHT
	    /* Weigh by absolute value of force */
	    forces[n_i + 0] /= FORCE_EPS + atom->absforce;
	    forces[n_i + 1] /= FORCE_EPS + atom->absforce;
	    forces[n_i + 2] /= FORCE_EPS + atom->absforce;
#endif /* FWEIGHT */

	    /* sum up forces */
#ifdef CONTRIB
	    if (atom->contrib)
#endif /* CONTRIB */
	      tmpsum += conf_weight[h] *
		(dsquare(forces[n_i + 0]) + dsquare(forces[n_i + 1]) + dsquare(forces[n_i + 2]));
	  }
	}
	/* end third loop over all atoms */

	/* energy contributions */
	forces[energy_p + h] /= (double)inconf[h];
	forces[energy_p + h] -= force_0[energy_p + h];
	tmpsum += conf_weight[h] * eweight * dsquare(forces[energy_p + h]);

#ifdef STRESS
	/* stress contributions */
	if (uf && us) {
	  for (i = 0; i < 6; i++) {
	    forces[stresses + i] /= conf_vol[h - firstconf];
	    forces[stresses + i] -= force_0[stresses + i];
	    tmpsum += conf_weight[h] * sweight * dsquare(forces[stresses + i]);
	  }
	}
#endif /* STRESS */

      }				/* loop over configurations */
    }				/* parallel region */

#ifdef APOT
    /* add punishment for out of bounds (mostly for powell_lsq) */
    if (myid == 0)
      tmpsum += apot_punish(xi_opt, forces);
#endif /* APOT */

    sum = tmpsum;		/* global sum = local sum  */
#ifdef MPI
    /* reduce global sum */
    sum = 0.0;
    MPI_Reduce(&tmpsum, &sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    /* gather forces, energies, stresses */
    if (myid == 0) {		/* root node already has data in place */
      /* forces */
      MPI_Gatherv(MPI_IN_PLACE, myatoms, MPI_VECTOR, forces, atom_len,
	atom_dist, MPI_VECTOR, 0, MPI_COMM_WORLD);
      /* energies */
      MPI_Gatherv(MPI_IN_PLACE, myconf, MPI_DOUBLE, forces + natoms * 3,
	conf_len, conf_dist, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      /* stresses */
      MPI_Gatherv(MPI_IN_PLACE, myconf, MPI_STENS, forces + natoms * 3 + nconf,
	conf_len, conf_dist, MPI_STENS, 0, MPI_COMM_WORLD);
    } else {
      /* forces */
      MPI_Gatherv(forces + firstatom * 3, myatoms, MPI_VECTOR, forces, atom_len,
	atom_dist, MPI_VECTOR, 0, MPI_COMM_WORLD);
      /* energies */
      MPI_Gatherv(forces + natoms * 3 + firstconf, myconf, MPI_DOUBLE,
	forces + natoms * 3, conf_len, conf_dist, MPI_DOUBLE, 0, MPI_COMM_WORLD);
      /* stresses */
      MPI_Gatherv(forces + natoms * 3 + nconf + 6 * firstconf, myconf, MPI_STENS,
	forces + natoms * 3 + nconf, conf_len, conf_dist, MPI_STENS, 0, MPI_COMM_WORLD);
    }
#endif /* MPI */

    /* root process exits this function now */
    if (myid == 0) {
      fcalls++;			/* Increase function call counter */
      if (isnan(sum)) {
#ifdef DEBUG
	printf("\n--> Force is nan! <--\n\n");
#endif /* DEBUG */
	return 10e10;
      } else
	return sum;
    }

  }				/* end of infinite loop */

  /* once a non-root process arrives here, all is done. */
  return -1.;
}

void update_tersoff_pointers(double *xi)
{
  int   i;
  int   index = 2;
  tersoff_t *tersoff = &apot_table.tersoff;

  /* allocate if this has not been done */
  if (0 == tersoff->init) {
    tersoff->A = (double **)malloc(paircol * sizeof(double *));
    tersoff->B = (double **)malloc(paircol * sizeof(double *));
    tersoff->lambda = (double **)malloc(paircol * sizeof(double *));
    tersoff->mu = (double **)malloc(paircol * sizeof(double *));
    tersoff->gamma = (double **)malloc(paircol * sizeof(double *));
    tersoff->n = (double **)malloc(paircol * sizeof(double *));
    tersoff->c = (double **)malloc(paircol * sizeof(double *));
    tersoff->d = (double **)malloc(paircol * sizeof(double *));
    tersoff->h = (double **)malloc(paircol * sizeof(double *));
    tersoff->S = (double **)malloc(paircol * sizeof(double *));
    tersoff->R = (double **)malloc(paircol * sizeof(double *));
    tersoff->chi = (double **)malloc(paircol * sizeof(double *));
    tersoff->omega = (double **)malloc(paircol * sizeof(double *));
    tersoff->c2 = (double *)malloc(paircol * sizeof(double));
    tersoff->d2 = (double *)malloc(paircol * sizeof(double));
    for (i = 0; i < paircol; i++) {
      tersoff->A[i] = NULL;
      tersoff->B[i] = NULL;
      tersoff->lambda[i] = NULL;
      tersoff->mu[i] = NULL;
      tersoff->gamma[i] = NULL;
      tersoff->n[i] = NULL;
      tersoff->c[i] = NULL;
      tersoff->d[i] = NULL;
      tersoff->h[i] = NULL;
      tersoff->S[i] = NULL;
      tersoff->R[i] = NULL;
      tersoff->chi[i] = NULL;
      tersoff->omega[i] = NULL;
      tersoff->c2[i] = 0.0;
      tersoff->d2[i] = 0.0;
    }
    tersoff->init = 1;
    tersoff->one = 1.0;
  }

  /* update only if the address has changed */
  if (tersoff->A[0] != xi + index) {
    /* set the pair parameters */
    for (i = 0; i < paircol; i++) {
      tersoff->A[i] = xi + index++;
      tersoff->B[i] = xi + index++;
      tersoff->lambda[i] = xi + index++;
      tersoff->mu[i] = xi + index++;
      tersoff->gamma[i] = xi + index++;
      tersoff->n[i] = xi + index++;
      tersoff->c[i] = xi + index++;
      tersoff->d[i] = xi + index++;
      tersoff->h[i] = xi + index++;
      tersoff->S[i] = xi + index++;
      tersoff->R[i] = xi + index++;
      index += 2;
    }
    for (i = 0; i < paircol; i++) {
      if (0 == (i % ntypes)) {
	tersoff->chi[i] = &tersoff->one;
	tersoff->omega[i] = &tersoff->one;
      } else {
	tersoff->chi[i] = xi + index++;
	tersoff->omega[i] = xi + index++;
	index += 2;
      }
    }
  }

  /* calculate c2 and d2 */
  for (i = 0; i < paircol; i++) {
    tersoff->c2[i] = *(tersoff->c[i]) * *(tersoff->c[i]);
    tersoff->d2[i] = *(tersoff->d[i]) * *(tersoff->d[i]);
  }

  return;
}

#endif /* TERSOFF */
