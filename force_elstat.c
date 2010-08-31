/****************************************************************
 *
 * force_elstat.c: Routines used for calculating pair/monopole/dipole
 *     forces/energies in various interpolation schemes.
 *
 ****************************************************************
 *
 * Copyright 2002-2010 Philipp Beck
 *	Institute for Theoretical and Applied Physics
 *	University of Stuttgart, D-70550 Stuttgart, Germany
 *	http://www.itap.physik.uni-stuttgart.de/
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

#ifdef COULOMB
#include "potfit.h"

/****************************************************************
 *
 *  compute forces using pair potentials with spline interpolation
 *
 *  returns sum of squares of differences between calculated and reference
 *     values
 *
 *  arguments: *xi - pointer to short-range potential
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

real calc_forces_elstat(real *xi_opt, real *forces, int flag)
{
  real  tmpsum, sum = 0.;
  int   first, col, ne, size, i;
  real *xi = NULL;
  apot_table_t *apt = &apot_table;
  real charges[ntypes];
  real sum_charges;
#ifdef DIPOLE
  FILE *outfile2;
  char *filename2 = "Dipole.convergency";
  int   sum_c = 0;
  int   sum_t = 0;
#endif

  switch (format) {
      case 0:
	xi = calc_pot.table;
	break;
      case 3:			/* fall through */
      case 4:
	xi = xi_opt;		/* calc-table is opt-table */
	break;
      case 5:
	xi = calc_pot.table;	/* we need to update the calc-table */
  }

  ne = apot_table.total_ne_par;
  size = apt->number;

  /* This is the start of an infinite loop */
  while (1) {
    tmpsum = 0.;		/* sum of squares of local process */
#ifndef APOT
    if (format > 4 && myid == 0)
      update_calc_table(xi_opt, xi, 0);
#endif /* APOT */

#if defined APOT && !defined MPI
    if (format == 0) {
      apot_check_params(xi_opt);
      update_calc_table(xi_opt, xi, 0);
    }
#endif

#ifdef MPI
    /* exchange potential and flag value */
#ifndef APOT
    MPI_Bcast(xi, calc_pot.len, REAL, 0, MPI_COMM_WORLD);
#endif /* APOT */
    MPI_Bcast(&flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

#ifdef APOT
    if (myid == 0)
      apot_check_params(xi_opt);
    MPI_Bcast(xi_opt, ndimtot, REAL, 0, MPI_COMM_WORLD);
    if (format == 0)
      update_calc_table(xi_opt, xi, 0);
    if (flag == 1)
      break;
#else /* APOT */

    /* if flag==2 then the potential parameters have changed -> sync */
    if (flag == 2)
      potsync();
    /* non root processes hang on, unless...  */
    if (flag == 1)
      break;			/* Exception: flag 1 means clean up */
#endif /* APOT */
#endif /* MPI */

    sum_charges = 0;
    for (i = 0; i < ntypes - 1; i++) {
      if(xi_opt[2*size + ne + i]) {
      charges[i] = xi_opt[2*size + ne + i];
      sum_charges += apt->ratio[i] * charges[i];
      } else {
	charges[i] = 0.;
      }
    } 
    apt->last_charge = - sum_charges / apt->ratio[ntypes - 1]; 
    charges[ntypes - 1] = apt->last_charge;

    /* init second derivatives for splines */
    for (col = 0; col < paircol; col++) {
      first = calc_pot.first[col];
      if (format == 3 || format == 0) {
	spline_ed(calc_pot.step[col], xi + first,
	  calc_pot.last[col] - first + 1,
	  *(xi + first - 2), 0.0, calc_pot.d2tab + first);
      } else {			/* format >= 4 ! */
	spline_ne(calc_pot.xcoord + first, xi + first,
	  calc_pot.last[col] - first + 1,
	  *(xi + first - 2), 0.0, calc_pot.d2tab + first);
      }
    }

#ifndef MPI
    myconf = nconf;
#endif

    /* region containing loop over configurations,
       also OMP-parallelized region */
    {
      int   self;
      vector tmp_force;
      int   h, j, k, l, typ1, typ2, uf, us, stresses;	/* config */
      real  fnval, grad, fnval_tail, grad_tail, grad_i, grad_j, p_sr_tail;
      atom_t *atom;
      neigh_t *neigh;

      /* loop over configurations: M A I N LOOP CONTAINING ALL ATOM-LOOPS */
      for (h = firstconf; h < firstconf + myconf; h++) {
	uf = conf_uf[h - firstconf];
	us = conf_us[h - firstconf];
	/* reset energies and stresses */
	forces[energy_p + h] = 0.;
	for (i = 0; i < 6; i++)
	  forces[stress_p + 6 * h + i] = 0.;

#ifdef DIPOLE
	/* reset dipoles and fields: LOOP Z E R O */
	for (i = cnfstart[h]; i < cnfstart[h] + inconf[h]; i++) {
	  atoms[i].E_stat.x = 0.;
	  atoms[i].E_stat.y = 0.;
	  atoms[i].E_stat.z = 0.;
	  atoms[i].p_sr.x = 0.;
	  atoms[i].p_sr.y = 0.;
	  atoms[i].p_sr.z = 0.;
	  atoms[i].E_ind.x = 0.;
	  atoms[i].E_ind.y = 0.;
	  atoms[i].E_ind.z = 0.;
	  atoms[i].p_ind.x = 0.;
	  atoms[i].p_ind.y = 0.;
	  atoms[i].p_ind.z = 0.;
	  atoms[i].E_old.x = 0.;
	  atoms[i].E_old.y = 0.;
	  atoms[i].E_old.z = 0.;
	  atoms[i].E_tot.x = 0.;
	  atoms[i].E_tot.y = 0.;
	  atoms[i].E_tot.z = 0.;
	}
#endif

	/* F I R S T LOOP OVER ATOMS: reset forces, dipoles */
	for (i = 0; i < inconf[h]; i++) {	/* atoms */
	  if (uf) {
	    k = 3 * (cnfstart[h] + i);
	    forces[k] = -force_0[k];
	    forces[k + 1] = -force_0[k + 1];
	    forces[k + 2] = -force_0[k + 2];
	  } else {
	    k = 3 * (cnfstart[h] + i);
	    forces[k] = 0.;
	    forces[k + 1] = 0.;
	    forces[k + 2] = 0.;
	  }
	}			/* end F I R S T LOOP */

	/* S E C O N D loop: calculate short-range and monopole forces,
	   calculate static field- and dipole-contributions */
	for (i = 0; i < inconf[h]; i++) {	/* atoms */
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  k = 3 * (cnfstart[h] + i);
	  for (j = 0; j < atom->n_neigh; j++) {	/* neighbors */
	    neigh = atom->neigh + j;
	    typ2 = neigh->typ;
	    col = neigh->col[0];

	    /* In small cells, an atom might interact with itself */
	    self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;

	    /* calculate short-range forces */
	    if (neigh->r < calc_pot.end[col]) {


	      if (uf) {
		fnval =
		  splint_comb_dir(&calc_pot, xi, neigh->slot[0],
		  neigh->shift[0], neigh->step[0], &grad);
	      } else {
		fnval =
		  splint_dir(&calc_pot, xi, neigh->slot[0], neigh->shift[0],
		  neigh->step[0]);
	      }

	      /* avoid double counting if atom is interacting with a
	         copy of itself */
	      if (self) {
		fnval *= 0.5;
		grad *= 0.5;
	      }
	      forces[energy_p + h] += fnval;

	      if (uf) {
		tmp_force.x = neigh->dist.x * grad;
		tmp_force.y = neigh->dist.y * grad;
		tmp_force.z = neigh->dist.z * grad;
		forces[k] += tmp_force.x;
		forces[k + 1] += tmp_force.y;
		forces[k + 2] += tmp_force.z;
		/* actio = reactio */
		l = 3 * neigh->nr;
		forces[l] -= tmp_force.x;
		forces[l + 1] -= tmp_force.y;
		forces[l + 2] -= tmp_force.z;

#ifdef STRESS
		/* calculate pair stresses */
		if (us) {
		  tmp_force.x *= neigh->r;
		  tmp_force.y *= neigh->r;
		  tmp_force.z *= neigh->r;
		  stresses = stress_p + 6 * h;
		  forces[stresses] -= neigh->dist.x * tmp_force.x;
		  forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		  forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		  forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		  forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		  forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
		}
#endif /* STRESS */
	      }
	    }

	    /* calculate monopole forces */
	    if (neigh->r < dp_cut && (charges[typ1]
		|| charges[typ2])) {

	      fnval_tail = neigh->fnval_el;
	      grad_tail = neigh->grad_el;

	      // printf("%lf\t%lf\n", fnval_tail, grad_tail);

	      grad_i = charges[typ2] * grad_tail;
	      if (typ1 == typ2) {
		grad_j = grad_i;
	      } else {
		grad_j = charges[typ1] * grad_tail;
	      }
	      fnval = charges[typ1] * charges[typ2] * fnval_tail;
	      grad = charges[typ1] * grad_i;

	      if (self) {
		grad_i *= 0.5;
		grad_j *= 0.5;
		fnval *= 0.5;
		grad *= 0.5;
	      }

	      forces[energy_p + h] += fnval;

	      if (uf) {
		tmp_force.x = neigh->dist.x * grad * neigh->r;
		tmp_force.y = neigh->dist.y * grad * neigh->r;
		tmp_force.z = neigh->dist.z * grad * neigh->r;
		forces[k] += tmp_force.x;
		forces[k + 1] += tmp_force.y;
		forces[k + 2] += tmp_force.z;
		/* actio = reactio */
		l = 3 * neigh->nr;
		forces[l] -= tmp_force.x;
		forces[l + 1] -= tmp_force.y;
		forces[l + 2] -= tmp_force.z;

#ifdef STRESS
		/* calculate coulomb stresses */
		if (us) {
		  tmp_force.x *= neigh->r;
		  tmp_force.y *= neigh->r;
		  tmp_force.z *= neigh->r;
		  stresses = stress_p + 6 * h;
		  forces[stresses] -= neigh->dist.x * tmp_force.x;
		  forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		  forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		  forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		  forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		  forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
		}
#endif /* STRESS */
	      }
#ifdef DIPOLE
	      /* calculate static field-contributions */
	      atom->E_stat.x += neigh->dist.x * neigh->r * grad_i;
	      atom->E_stat.y += neigh->dist.y * neigh->r * grad_i;
	      atom->E_stat.z += neigh->dist.z * neigh->r * grad_i;
	      atoms[neigh->nr].E_stat.x -= neigh->dist.x * neigh->r * grad_j;
	      atoms[neigh->nr].E_stat.y -= neigh->dist.y * neigh->r * grad_j;
	      atoms[neigh->nr].E_stat.z -= neigh->dist.z * neigh->r * grad_j;

	      /* calculate short-range dipoles */
	      if ((xi_opt[2 * size + ne + ntypes + typ1 - 1])
		&& (xi_opt[2 * size + ne + 2 * ntypes + col - 1])
		&& (xi_opt[3 * size + ne + 2 * ntypes + col - 1])) {
		p_sr_tail = grad_tail * neigh->r *
		  shortrange_value(neigh->r,
		  xi_opt[2 * size + ne + ntypes + typ1 - 1],
		  xi_opt[2 * size + ne + 2 * ntypes + col - 1],
		  xi_opt[3 * size + ne + 2 * ntypes + col - 1]);
		atom->p_sr.x += charges[typ2] * neigh->dist.x * p_sr_tail;
		atom->p_sr.y += charges[typ2] * neigh->dist.y * p_sr_tail;
		atom->p_sr.z += charges[typ2] * neigh->dist.z * p_sr_tail;
	      }
	      if ((xi_opt[2 * size + ne + ntypes + typ2 - 1])
		&& (xi_opt[2 * size + ne + 2 * ntypes + col - 1])
		&& (xi_opt[3 * size + ne + 2 * ntypes + col - 1]) && !self) {
		p_sr_tail = grad_tail * neigh->r *
		  shortrange_value(neigh->r,
		  xi_opt[2 * size + ne + ntypes + typ2 - 1],
		  xi_opt[2 * size + ne + 2 * ntypes + col - 1],
		  xi_opt[3 * size + ne + 2 * ntypes + col - 1]);
		atoms[neigh->nr].p_sr.x -= charges[typ1] * neigh->dist.x * p_sr_tail;
		atoms[neigh->nr].p_sr.y -= charges[typ1] * neigh->dist.y * p_sr_tail;
		atoms[neigh->nr].p_sr.z -= charges[typ1] * neigh->dist.z * p_sr_tail;
	      }
#endif /* DIPOLE */

	    }

	  }			/* loop over neighbours */
	}			/* end S E C O N D loop over atoms */


#ifdef DIPOLE
	/* T H I R D loop: calculate whole dipole moment for every atom */
	real  rp, dp_sum;
	int   dp_converged = 0, dp_it = 0;
	real  max_diff = 10;

	while (dp_converged == 0) {
	  dp_sum = 0;
	  for (i = 0; i < inconf[h]; i++) {	/* atoms */
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;
	    
	    if (xi_opt[2 * size + ne + ntypes + typ1 - 1]) {

	      if(dp_it){
	      atom->E_tot.x =
		(1 - dp_mix) * atom->E_ind.x + dp_mix * atom->E_old.x + atom->E_stat.x;
	      atom->E_tot.y =
		(1 - dp_mix) * atom->E_ind.y + dp_mix * atom->E_old.y + atom->E_stat.y;
	      atom->E_tot.z =
		(1 - dp_mix) * atom->E_ind.z + dp_mix * atom->E_old.z + atom->E_stat.z;
	      } else {
		atom->E_tot.x = atom->E_ind.x + atom->E_stat.x;
		atom->E_tot.y = atom->E_ind.y + atom->E_stat.y;
		atom->E_tot.z = atom->E_ind.z + atom->E_stat.z;
	      }

	      atom->p_ind.x =
		xi_opt[2 * size + ne + ntypes + typ1 - 1] *  atom->E_tot.x + atom->p_sr.x;
	      atom->p_ind.y =
		xi_opt[2 * size + ne + ntypes + typ1 - 1] *  atom->E_tot.y + atom->p_sr.y;
	      atom->p_ind.z =
		xi_opt[2 * size + ne + ntypes + typ1 - 1] *  atom->E_tot.z + atom->p_sr.z;

	      atom->E_old.x = atom->E_ind.x;
	      atom->E_old.y = atom->E_ind.y;
	      atom->E_old.z = atom->E_ind.z;
	    }
	  }


	  for (i = 0; i < inconf[h]; i++) {	/* atoms */
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;
	    for (j = 0; j < atom->n_neigh; j++) {	/* neighbors */
	      neigh = atom->neigh + j;
	      typ2 = neigh->typ;
	      col = neigh->col[0];
	      /* In small cells, an atom might interact with itself */
	      self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;

	      if (neigh->r < dp_cut) {
		if (xi_opt[2 * size + ne + ntypes + typ1 - 1] && xi_opt[2 * size + ne + ntypes + typ2 - 1]) {
		  rp = SPROD(atoms[neigh->nr].p_ind, neigh->dist);
		  atom->E_ind.x += neigh->grad_el 
		    * (3 * rp * neigh->dist.x - atoms[neigh->nr].p_ind.x);
		  atom->E_ind.y += neigh->grad_el
		    * (3 * rp * neigh->dist.y - atoms[neigh->nr].p_ind.y);		   
		  atom->E_ind.z += neigh->grad_el 
		    * (3 * rp * neigh->dist.z - atoms[neigh->nr].p_ind.z);
		   
		}
		if (xi_opt[2 * size + ne + ntypes + typ1 - 1] && !self) {
		  rp = SPROD(atom->p_ind, neigh->dist);
		  atoms[neigh->nr].E_ind.x += neigh->grad_el 
		    * (3 * rp * neigh->dist.x - atom->p_ind.x);
		  atoms[neigh->nr].E_ind.y += neigh->grad_el 
		    * (3 * rp * neigh->dist.y - atom->p_ind.y);	          
		  atoms[neigh->nr].E_ind.z += neigh->grad_el 
		    * (3 * rp * neigh->dist.z - atom->p_ind.z);		  
		}
	      }
	    }
	  }

	  for (i = 0; i < inconf[h]; i++) {	/* atoms */
	    atom = conf_atoms + i + cnfstart[h] - firstatom;
	    typ1 = atom->typ;
	    if (xi_opt[2 * size + ne + ntypes + typ1 - 1]) {
	      dp_sum += SQR(atom->E_old.x - atom->E_ind.x);
	      dp_sum += SQR(atom->E_old.y - atom->E_ind.y);
	      dp_sum += SQR(atom->E_old.z - atom->E_ind.z);
	    }
	  }

	  dp_sum *= SQR(xi_opt[2 * size + ne + ntypes + typ1 - 1]);
	  dp_sum /= 3 * inconf[h];
	  dp_sum = sqrt(dp_sum);

	  if (dp_it) {
	    if ((dp_sum > max_diff) || (dp_it > 50)) {
	      sum_c += 50;
	      dp_converged = 1;
	      for (i = 0; i < inconf[h]; i++) {	/* atoms */
		atom = conf_atoms + i + cnfstart[h] - firstatom;
		typ1 = atom->typ;
		if (xi_opt[2 * size + ne + ntypes + typ1 - 1]) {
		  atom->p_ind.x =
		    xi_opt[2 * size + ne + ntypes + typ1 - 1] * atom->E_stat.x +
		    atom->p_sr.x;
		  atom->p_ind.y =
		    xi_opt[2 * size + ne + ntypes + typ1 - 1] * atom->E_stat.y +
		    atom->p_sr.y;
		  atom->p_ind.z =
		    xi_opt[2 * size + ne + ntypes + typ1 - 1] * atom->E_stat.z +
		    atom->p_sr.z;
		  atom->E_ind.x = atom->E_stat.x;
		  atom->E_ind.y = atom->E_stat.y;
		  atom->E_ind.z = atom->E_stat.z;
		}
	      }
	    }
	  }

	  if (dp_sum < dp_tol) {
	    dp_converged = 1;
	    sum_c += dp_it;
	  }

	  dp_it++;
	}			/* end T H I R D loop over atoms */


	// for (i = 0; i < inconf[h]; i++) {
	// atom = conf_atoms + i + cnfstart[h] - firstatom;
	// printf("%d\t%d\t%f\t%f\t%f\n", i, atom->typ, atom->p_ind.x, atom->p_ind.y, atom->p_ind.z);
	// }


	/* F O U R T H  loop: calculate monopole-dipole and dipole-dipole forces */
	real  rp_i, rp_j, pp_ij, tmp_1, tmp_2;
	real  grad_1, grad_2, srval, srgrad, srval_tail, srgrad_tail, fnval_sum, grad_sum;
	for (i = 0; i < inconf[h]; i++) {	/* atoms */
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  k = 3 * (cnfstart[h] + i);
	  for (j = 0; j < atom->n_neigh; j++) {	/* neighbors */
	    neigh = atom->neigh + j;
	    typ2 = neigh->typ;
	    col = neigh->col[0];

	    /* In small cells, an atom might interact with itself */
	    self = (neigh->nr == i + cnfstart[h]) ? 1 : 0;
	    if (neigh->r < dp_cut && (xi_opt[2 * size + ne + ntypes + typ1 - 1]
		|| xi_opt[2 * size + ne + ntypes + typ2 - 1])) {

	      fnval_tail = -neigh->grad_el;
	      grad_tail = -neigh->ggrad_el;

	      // printf("%f\t%f\n", fnval_tail, grad_tail);

	      if (xi_opt[2 * size + ne + 2 * ntypes + col - 1]
		  && xi_opt[3 * size + ne + 2 * ntypes + col - 1]) {
		shortrange_term(neigh->r, xi_opt[2 * size + ne + 2 * ntypes + col - 1], 
				xi_opt[3 * size + ne + 2 * ntypes + col - 1], 
				&srval_tail, &srgrad_tail);
		srval = fnval_tail * srval_tail;
		srgrad = fnval_tail * srgrad_tail + grad_tail * srval_tail;
		}
	      
	      if (self) {
		fnval_tail *= 0.5;
		grad_tail *= 0.5;
	      }

	      /* monopole-dipole contributions */
	      if (charges[typ1] && xi_opt[2 * size + ne + ntypes + typ2 - 1]) {
		
		if (xi_opt[2 * size + ne + 2 * ntypes + col - 1]
		    && xi_opt[3 * size + ne + 2 * ntypes + col - 1]) {
		  fnval_sum = fnval_tail + srval;
		  grad_sum = grad_tail + srgrad;
		} else {
		  fnval_sum = fnval_tail;
		  grad_sum = grad_tail;
		}

		rp_j = SPROD(atoms[neigh->nr].p_ind, neigh->dist);
		fnval = charges[typ1] * rp_j * fnval_sum * neigh->r;
		grad_1 = charges[typ1] * rp_j * grad_sum * neigh->r2;
		grad_2 = charges[typ1] * fnval_sum;

		forces[energy_p + h] -= fnval;

		if (uf) {
		  tmp_force.x =
		    neigh->dist.x * grad_1 +
		    atoms[neigh->nr].p_ind.x * grad_2;
		  tmp_force.y =
		    neigh->dist.y * grad_1 +
		    atoms[neigh->nr].p_ind.y * grad_2;
		  tmp_force.z =
		    neigh->dist.z * grad_1 +
		    atoms[neigh->nr].p_ind.z * grad_2;
		  forces[k] -= tmp_force.x;
		  forces[k + 1] -= tmp_force.y;
		  forces[k + 2] -= tmp_force.z;
		  /* actio = reactio */
		  l = 3 * neigh->nr;
		  forces[l] += tmp_force.x;
		  forces[l + 1] += tmp_force.y;
		  forces[l + 2] += tmp_force.z;

#ifdef STRESS
		  /* calculate stresses */
		  if (us) {
		    tmp_force.x *= neigh->r;
		    tmp_force.y *= neigh->r;
		    tmp_force.z *= neigh->r;
		    stresses = stress_p + 6 * h;
		    forces[stresses] += neigh->dist.x * tmp_force.x;
		    forces[stresses + 1] += neigh->dist.y * tmp_force.y;
		    forces[stresses + 2] += neigh->dist.z * tmp_force.z;
		    forces[stresses + 3] += neigh->dist.x * tmp_force.y;
		    forces[stresses + 4] += neigh->dist.y * tmp_force.z;
		    forces[stresses + 5] += neigh->dist.z * tmp_force.x;
		  }
#endif /* STRESS */
		}
	      }


	      /* dipole-monopole contributions */
	      if (xi_opt[2 * size + ne + ntypes + typ1 - 1] && charges[typ2]) {
	
		if (xi_opt[2 * size + ne + 2 * ntypes + col - 1]
		    && xi_opt[3 * size + ne + 2 * ntypes + col - 1]) {
		  fnval_sum = fnval_tail + srval;
		  grad_sum = grad_tail + srgrad;
		} else {
		  fnval_sum = fnval_tail;
		  grad_sum = grad_tail;
		}

		rp_i = SPROD(atom->p_ind, neigh->dist);
		fnval = charges[typ2] * rp_i * fnval_sum * neigh->r;
		grad_1 = charges[typ2] * rp_i * grad_sum * neigh->r2;
		grad_2 = charges[typ2] * fnval_sum;

		forces[energy_p + h] += fnval;

		if (uf) {
		  tmp_force.x =
		    neigh->dist.x * grad_1 + atom->p_ind.x * grad_2;
		  tmp_force.y =
		    neigh->dist.y * grad_1 + atom->p_ind.y * grad_2;
		  tmp_force.z =
		    neigh->dist.z * grad_1 + atom->p_ind.z * grad_2;
		  forces[k] += tmp_force.x;
		  forces[k + 1] += tmp_force.y;
		  forces[k + 2] += tmp_force.z;
		  /* actio = reactio */
		  l = 3 * neigh->nr;
		  forces[l] -= tmp_force.x;
		  forces[l + 1] -= tmp_force.y;
		  forces[l + 2] -= tmp_force.z;

#ifdef STRESS
		  /* calculate stresses */
		  if (us) {
		    tmp_force.x *= neigh->r;
		    tmp_force.y *= neigh->r;
		    tmp_force.z *= neigh->r;
		    stresses = stress_p + 6 * h;
		    forces[stresses] -= neigh->dist.x * tmp_force.x;
		    forces[stresses + 1] -= neigh->dist.y * tmp_force.y;
		    forces[stresses + 2] -= neigh->dist.z * tmp_force.z;
		    forces[stresses + 3] -= neigh->dist.x * tmp_force.y;
		    forces[stresses + 4] -= neigh->dist.y * tmp_force.z;
		    forces[stresses + 5] -= neigh->dist.z * tmp_force.x;
		  }
#endif /* STRESS */
		}
	      }


	      /* dipole-dipole contributions */
	      if (xi_opt[2 * size + ne + ntypes + typ1 - 1]
		&& xi_opt[2 * size + ne + ntypes + typ2 - 1]) {
		
		pp_ij = SPROD(atom->p_ind, atoms[neigh->nr].p_ind);
		tmp_1 = 3 * rp_i * rp_j;
		tmp_2 = 3 * fnval_tail / neigh->r2;

		fnval = -(tmp_1 - pp_ij) * fnval_tail;
		grad_1 = (tmp_1 - pp_ij) * grad_tail;
		grad_2 = 2 * rp_i * rp_j;

		forces[energy_p + h] += fnval;

		if (uf) {
		  tmp_force.x = grad_1 * neigh->r * neigh->dist.x - tmp_2 
		    * (grad_2 * neigh->r * neigh->dist.x - rp_i * neigh->r * atoms[neigh->nr].p_ind.x - rp_j * neigh->r * atom->p_ind.x);
		  tmp_force.y = grad_1 * neigh->r * neigh->dist.y - tmp_2 
		    * (grad_2 * neigh->r * neigh->dist.y - rp_i * neigh->r * atoms[neigh->nr].p_ind.y - rp_j * neigh->r * atom->p_ind.y);
		  tmp_force.z = grad_1 * neigh->r * neigh->dist.z - tmp_2 
		    * (grad_2 * neigh->r * neigh->dist.z - rp_i * neigh->r * atoms[neigh->nr].p_ind.z - rp_j * neigh->r * atom->p_ind.z);
		  forces[k] -= tmp_force.x;
		  forces[k + 1] -= tmp_force.y;
		  forces[k + 2] -= tmp_force.z;
		  /* actio = reactio */
		  l = 3 * neigh->nr;
		  forces[l] += tmp_force.x;
		  forces[l + 1] += tmp_force.y;
		  forces[l + 2] += tmp_force.z;

#ifdef STRESS
		  /* calculate stresses */
		  if (us) {
		    tmp_force.x *= neigh->r;
		    tmp_force.y *= neigh->r;
		    tmp_force.z *= neigh->r;
		    stresses = stress_p + 6 * h;
		    forces[stresses] += neigh->dist.x * tmp_force.x;
		    forces[stresses + 1] += neigh->dist.y * tmp_force.y;
		    forces[stresses + 2] += neigh->dist.z * tmp_force.z;
		    forces[stresses + 3] += neigh->dist.x * tmp_force.y;
		    forces[stresses + 4] += neigh->dist.y * tmp_force.z;
		    forces[stresses + 5] += neigh->dist.z * tmp_force.x;
		  }
#endif /* STRESS */
		}
	      }

	    }
	  }			/* loop over neighbours */
	}			/* end F O U R T H loop over atoms */
#endif /* DIPOLE */


	/* F I F T H  loop: self energy contributions and sum-up force contributions */
	real  qq, pp;
	for (i = 0; i < inconf[h]; i++) {	/* atoms */
	  atom = conf_atoms + i + cnfstart[h] - firstatom;
	  typ1 = atom->typ;
	  k = 3 * (cnfstart[h] + i);

	  /* self energy contributions */
	  if (charges[typ1]) {
	    qq = charges[typ1] * charges[typ1];
	    fnval = dp_eps * dp_kappa * qq / sqrt(M_PI);
	    forces[energy_p + h] -= fnval;
	  }
#ifdef DIPOLE
	  if (xi_opt[2 * size + ne + ntypes + typ1 - 1]) {
	    pp = SPROD(atom->p_ind, atom->p_ind);
	    fnval = pp / (2 * xi_opt[2 * size + ne + ntypes + typ1 - 1]);
	    forces[energy_p + h] += fnval;
	  }
#endif


	  /* sum-up: whole force contributions flow into tmpsum */
	  if (uf) {
#ifdef FWEIGHT
	    /* Weigh by absolute value of force */
	    forces[k] /= FORCE_EPS + atom->absforce;
	    forces[k + 1] /= FORCE_EPS + atom->absforce;
	    forces[k + 2] /= FORCE_EPS + atom->absforce;
#endif
	    tmpsum +=
	      conf_weight[h] * (SQR(forces[k]) + SQR(forces[k + 1]) +
	      SQR(forces[k + 2]));
	  }

	}			/* end F I F T H loop over atoms */


	/* whole energy contributions flow into tmpsum */
	forces[energy_p + h] *= eweight / (real)inconf[h];
	forces[energy_p + h] -= force_0[energy_p + h];
	tmpsum += conf_weight[h] * SQR(forces[energy_p + h]);

#ifdef STRESS
	/* whole stress contributions flow into tmpsum */
	if (uf && us) {
	  for (i = 0; i < 6; i++) {
	    forces[stress_p + 6 * h + i] *= sweight / conf_vol[h - firstconf];
	    forces[stress_p + 6 * h + i] -= force_0[stress_p + 6 * h + i];
	    tmpsum += conf_weight[h] * SQR(forces[stress_p + 6 * h + i]);
	  }
	}
#endif

      }				/* end M A I N loop over configurations */

#ifdef DIPOLE
      /* output for "Dipol_Konvergenz_Verlauf" */
      if (myid == 0) {
	sum_t = sum_c / h;
	outfile2 = fopen(filename2, "a");
	fprintf(outfile2, "%d\n", sum_t);
	fclose(outfile2);
      }
#endif

    }				/* parallel region */

    /* dummy constraints (global) */
#ifdef APOT
    /* add punishment for out of bounds (mostly for powell_lsq) */
    if (myid == 0) {
      tmpsum += apot_punish(xi_opt, forces);
    }
#endif

    sum = tmpsum;		/* global sum = local sum  */

#ifdef MPI
    /* reduce global sum */
    sum = 0.;
    MPI_Reduce(&tmpsum, &sum, 1, REAL, MPI_SUM, 0, MPI_COMM_WORLD);
    /* gather forces, energies, stresses */
    MPI_Gatherv(forces + firstatom * 3, myatoms, MPI_VEKTOR,	/* forces */
      forces, atom_len, atom_dist, MPI_VEKTOR, 0, MPI_COMM_WORLD);
    MPI_Gatherv(forces + natoms * 3 + firstconf, myconf, REAL,	/* energies */
      forces + natoms * 3, conf_len, conf_dist, REAL, 0, MPI_COMM_WORLD);
    /* stresses */
    MPI_Gatherv(forces + natoms * 3 + nconf +
      6 * firstconf, myconf, MPI_STENS,
      forces + natoms * 3 + nconf,
      conf_len, conf_dist, MPI_STENS, 0, MPI_COMM_WORLD);
#endif /* MPI */

    /* root process exits this function now */
    if (myid == 0) {
      fcalls++;			/* Increase function call counter */
      if (isnan(sum)) {
#ifdef DEBUG
	printf("\n--> Force is nan! <--\n\n");
#endif
	return 10e10;
      } else
	return sum;
    }

  }

  /* once a non-root process arrives here, all is done. */
  return -1.;
}

#endif /* COULOMB */
